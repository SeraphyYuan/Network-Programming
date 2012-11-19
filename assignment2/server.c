#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/file.h>
#include <setjmp.h>
#include <math.h>
#include "unp.h"
#include "unpthread.h"
#include "unpifiplus.h"

/* DEFINE ALL THE THINGS! */
#define RTT_RXTMIN		1000 			/* 1 second (in msec) */
#define RTT_RXTMAX		3000			/* 3 seconds (in msec) */
#define RTT_MAXNREXMT	12 				/* maximum amount of times to retransmit */
#define HEADER_SIZE		20 				/* size of the header */

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
extern void  		    free_ifi_info_plus(struct ifi_info *ifihead);

/* THE STRUCTURES! */
/* Structure for storing server interfaces' properties */
struct server_interface{
	int 				fd;				/* socket file descriptor */
	struct	sockaddr  	*ifi_addr;		/* primary address */
	struct	sockaddr  	*ifi_ntmaddr;  	/* network mask address */
	struct	sockaddr	*ifi_subaddr;	/* subnet mask address */
};

/* Structure for the header (use htonl/ntohl) */
struct header{
	uint32_t  			seq_num;		/* this is really packet number */
	uint32_t 			ack;			/* set to 1 to have ack flag on */
	uint32_t 			fin;			/* fin flag - used to show last packet in transfer */
	uint32_t 			receive_window;	/* the receive window space at this point */
	uint32_t			size; 			/* size of the packet body */
};

/* Structure for storing the clients signatures */
struct linked_list{
	uint32_t 			ip;				/* ip of the client */
	uint32_t			port;			/* port of the client */
	struct linked_list	*next;			/* next item in the list */
	pid_t				pid;			/* process id */
	int 				pipefd;			/* pipe between child and parent */
};

/* Structure for storing packets in-flight */
struct sent_packet{
	struct sent_packet 	*next;			/* pointer to the next packet */
	struct header 		packet_header;
	char 				body[512];
	int 				acks;			/* used to store cumulative acks */
	int 				times_sent;		/* used to store number of times sent */
	unsigned long long int ts_msec; 	/* time that packet was sent in msec since epoch */
};

/* Structure for storing rtt information */
struct rtt_info{
	int 				rtt;			/* most recent rtt in msec */
	int 				srtt;			/* smoothed rtt in msec inflated by 8X */
	int 				rttvar; 		/* variance inflated by 4X */
	int 				rto;			/* current calculation to use in msec */
	int 				windowPing;		/* how long until sending the next window ping in msec */
};

/* THE FUNCTIONS! */
static void 			sendto_alarm(int);
struct linked_list		*is_client(uint32_t ip, uint32_t port);
void 					add_client(uint32_t ip, uint32_t port, pid_t pid, int lepipe);
void 					remove_client(uint32_t ip, uint32_t port);
void 					remove_client_pid(pid_t pid);
void 					sig_child(int signo);
Sigfunc			 		*signal(int signo, Sigfunc *func);
Sigfunc 				*SignalSteve(int signo, Sigfunc *func);
void 					process_acks(int seq_num, struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt, unsigned int *packetsInFlight, int connfd, struct sockaddr_in *cliaddr, int *cwnd, int *ssthresh, int *ackCntr, int *rwind_init);
void 					remove_packet(int seq_num, struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt, unsigned int *packetsInFlight);
void 					do_rtt(unsigned long dif, struct rtt_info **rtt);
unsigned long long int 	get_msec();
int 					rtt_minmax(int rtt);
void 					reset_head_timer(struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt);
void 					add_packet(struct sent_packet *dgram_packet, struct sent_packet **windowHead, unsigned int *packetsInFlight);
void 					send_packet(struct sent_packet *dgram_packet, int connfd, struct sent_packet **windowHead, struct sockaddr_in *cliaddr, struct itimerval **timer, struct rtt_info **rtt);

//Function that does the child work
void do_work(struct server_interface if_list[], int index, struct sockaddr_in *cliaddr, int num_interfaces, char requstedFile[], int pipefd, int send_window, int receive_window);


/* GLOBAL VARIABLES! */
struct linked_list 			*head = NULL; 		/* stores the head of the linked list */
static sigjmp_buf 			jmpbuf;				/* used for the rto timer */

/* MAIN SERVER! */
int main(int argc, char **argv)
{
	const char 					serverConfig[] = "server.in";
	const int 					on = 1;				/* 'on' flag */
	FILE 						*configFile;		/* pointer to the file descriptor */
	char						buf[MAXLINE];		
	short int 					port;				/* port server will accept connections on */
	int 						i, n, send_window;
	int 						maxfdp1; 			/* maximum socket file descriptor plus one */
	int 						num_interfaces = 0; /* counter used to store number of interfaces */
	struct ifi_info 	 		*ifi, *ifihead; 	/* ifi_info structures */
	fd_set 						fdset; 				/* set of FDs used for select */
	struct sockaddr_in 			cliaddr;			/* structure to put address of connecting client */
	socklen_t 					len;
	pid_t 						pid;				/* holds the process id for forking */
    struct timeval				selectWaitTime;		/* timeout for the select */
	char 						message[MAXLINE];
	
	bzero(&cliaddr, sizeof(cliaddr));
	cliaddr.sin_family = AF_INET;

    //signal for when a child process terminates (removes from the list)
    signal(SIGCHLD, sig_child);
	

	/* Read the input file and get the parameters */
	configFile = fopen(serverConfig, "r");
	if(configFile != NULL){
		//Get the port
		if(fgets(buf, sizeof(buf), configFile) != NULL){
			if(strlen(buf)<1){
				printf("Port not defined in file 'server.in'\n");
				exit(1);
			}
			port = atoi(buf);
		}else{
			printf("Port not defined in file 'server.in'\n");
			exit(1);
		}
		//Get the send_window
		if(fgets(buf, sizeof(buf), configFile) != NULL){
			if(strlen(buf)<1){
				printf("Maximum sending sliding-window size not defined in file 'server.in'\n");
				exit(1);
			}
			send_window = atoi(buf);
		}else{
			printf("Maximum sending sliding-window size not defined in file 'server.in'\n");
			exit(1);
		}
		fclose(configFile);
	}else{
		printf("Error opening file 'server.in': %s\n", strerror(errno));
		exit(1);
	}


	/* Start the server */
	printf("Server starting on port %i. . . \n", port);

	/* Get the number of interfaces */
	for(ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next){
		num_interfaces += 1;
	}
	struct server_interface if_list[num_interfaces];	/* array of server_interface items */

	/* Fill in the data structure and bind to the sockets */
	i = 0;
	for(ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next){
		if_list[i].ifi_addr = ifi->ifi_addr;			/* IP address */
		if_list[i].ifi_ntmaddr = ifi->ifi_ntmaddr;		/* Network mask address */

		//convert both ip addresses into unsigned ints and compute the subnet address:
		struct sockaddr_in *intaddr = (struct sockaddr_in *) if_list[i].ifi_addr;
		uint32_t uintaddr = intaddr->sin_addr.s_addr; 	/* pull address as integer from structure */

		struct sockaddr_in *intmask = (struct sockaddr_in *) if_list[i].ifi_ntmaddr;
		uint32_t uintmask = intmask->sin_addr.s_addr;	/* pull address as integer from structure */

		/* Make subnet address */
		uint32_t usubaddr = uintaddr & uintmask;  		/* bitwise AND of IP and Mask*/
		struct sockaddr_in subaddr;						/* create temp var to hold subnet address */
		bzero(&subaddr, sizeof(subaddr));				
		subaddr.sin_family = AF_INET;					/* set the family */
		subaddr.sin_addr.s_addr = usubaddr;				/* set the integer value for the addr */
		if_list[i].ifi_subaddr = (struct sockaddr *) &subaddr; /* add subnet address to structure */

		/* Print Interface Information */
		printf("\nInterface %s:\n", ifi->ifi_name);
		if(if_list[i].ifi_addr != NULL)
			printf("IP addr: %s\n", sock_ntop_host(if_list[i].ifi_addr, sizeof(*if_list[i].ifi_addr)));
		if(if_list[i].ifi_ntmaddr != NULL)
			printf("Network mask: %s\n", sock_ntop_host(if_list[i].ifi_ntmaddr, sizeof(*if_list[i].ifi_ntmaddr)));
		if(if_list[i].ifi_subaddr != NULL)
			printf("Subnet addr: %s\n", sock_ntop_host(if_list[i].ifi_subaddr, sizeof(*if_list[i].ifi_subaddr)));

		/* Create and Bind Sockets */
		if( (if_list[i].fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
			printf("error creating socket: %s\n", strerror(errno));
			exit(1);
		}

		/* set socket option to reuse the address */
		if(setsockopt(if_list[i].fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0){
			printf("error setting socket option to SO_REUSEADDR: %s\n", strerror(errno));
			exit(1);
		}
	
		/* bind the interface to the socket */
		intaddr->sin_family = AF_INET;
		intaddr->sin_port = htons(port);

		if(bind(if_list[i].fd, (SA *) intaddr, sizeof(*intaddr)) < 0){
			printf("error binding on %s with error: %s\n", 
				sock_ntop_host((SA *) intaddr, sizeof(*intaddr)), strerror(errno));
			exit(1);
		}else{
			printf("bind okay on ip: %s\n",sock_ntop_host((SA *) intaddr, sizeof(*intaddr)));
		}
		i += 1;
	}

	/* Select the socket that is readable */
	FD_ZERO(&fdset);
	for( ; ; ){
		maxfdp1 = 0;
		int j =0;

		for(j=0; j<num_interfaces; j++){			/* for each of the interfaces */
			FD_SET(if_list[j].fd, &fdset);
			maxfdp1 = max(if_list[j].fd, maxfdp1);	/* see if this file descriptor is largest so far */
		}
		maxfdp1 += 1; 								/* add one to the file descriptor */
		
		//1sec timeout
 	  	selectWaitTime.tv_sec = 1L;
    	selectWaitTime.tv_usec = 0L;
		if(select(maxfdp1, &fdset, NULL, NULL, &selectWaitTime) < 0){
			if(errno == EINTR){
				continue;
			}else{
        		//printf("error on select: %s\n", strerror(errno));
				exit(1);
			}
		}

		//check each interface to see if it can read:
		for(i=0; i<num_interfaces; i++){
			if(FD_ISSET(if_list[i].fd, &fdset)){
				//try to recvfrom:
				//set the socket address length
				struct header receive_header;
				len = sizeof(cliaddr);
				if((n = recvfrom(if_list[i].fd, buf, MAXLINE, 0, (struct sockaddr *) &cliaddr, &len)) <= 0){
					printf("recvfrom returned length %i from %s with error: %s\n", n, strerror(errno),
						sock_ntop_host((SA *) &cliaddr, sizeof(cliaddr)));
					continue;	/* nothing usefull was read */
				}else{
					//get the header:
					memcpy(&receive_header, &buf, sizeof(receive_header));

					memcpy(&message, &buf[HEADER_SIZE], ntohl(receive_header.size));
					message[ntohl(receive_header.size)] = '\0';

					printf("\nRequest for file '%s' received from %s\n", message, 
						sock_ntop_host((SA *) &cliaddr, sizeof(cliaddr)));

					/* Check if this client is already handled - and if it is tell the appropriate child */
					struct linked_list *clientNode;
					if( (clientNode = is_client(cliaddr.sin_addr.s_addr,cliaddr.sin_port)) != NULL){
						//pipe to child
						if(write(clientNode->pipefd, "RESEND", strlen("RESEND")) < 0){
				   			printf("Failed to write to pipe! Error %s\n", strerror(errno));
				   			exit(1);
				   		}
						continue;
					}
				}


				/* otherwise we fork a new child and create a pipe */
				int pipefd[2];
				if(pipe(pipefd) < 0){
					printf("Error creating pipe: %s\n", strerror(errno));
					exit(1);
				}
				if((pid = fork()) > 0){
					close(pipefd[0]); //parent closes the read end
					//add the client to the list
					add_client(cliaddr.sin_addr.s_addr,cliaddr.sin_port,pid,pipefd[1]);
				}else if(pid == 0){
					close(pipefd[1]); //child closes the write end
					//have the child do work:
					send_window = min(send_window, ntohl(receive_header.receive_window));
					printf("Server sending window size of %i chosen. . .", send_window);
					do_work(if_list, i, &cliaddr, num_interfaces, message, pipefd[0], send_window, ntohl(receive_header.receive_window)); 
					remove_client(cliaddr.sin_addr.s_addr,cliaddr.sin_port); /* remove the client from the list */
					close(pipefd[0]); /* close the read end of the pipe */
				}else if(pid < 0){
					printf("Error forking: %s\n", strerror(errno));
					exit(1);
				}
			}
			
		} /* check next interface */
	} /* reset select flags and check interfaces again */

	//server shutdown routines
	free_ifi_info_plus(ifihead);
	return 0;
}

/* ---------- FILE TRANSFER FUNCTION ---------- */
/* Does heavy lifting for interacting with the client */
void do_work(struct server_interface if_list[], int index, struct sockaddr_in *cliaddr, int num_interfaces, char requstedFile[], int pipefd, int send_window, int rwind_init){
	int isLocal = 0;
	int i;
	int on = 1;
	int connfd;
	struct sockaddr_in 	childaddr;
	int serverEPort;
	socklen_t local_size;
	struct sockaddr_in	localaddr;
	char buf[MAXLINE];
	fd_set fdset;
	int	maxfdp1;
	struct timeval selectWaitTime;
	struct header receive_header;
	uint32_t receive_window;
	char body[MAXLINE];
	char datagram[MAXLINE];
	FILE *requestFile;
	char bufFile[512];
	int bytesRead;
	struct sent_packet *windowHead = NULL;
	unsigned int packetsInFlight = 0;
	struct rtt_info	*rtt = (struct rtt_info*) malloc( sizeof(struct rtt_info) );
	struct itimerval timerv;
	struct itimerval *timer = &timerv;
	unsigned int packetNumber = 0;
	sigset_t newmask;

	/* Congestion control values */
	int cwnd = 1;				/* the number of packets can be in flight at any given moment */
	int ssthresh = rwind_init; 	/* the threshold for ending slow start at */
	int ackCntr = 0;			/* counter for saying how many packets of the current_packets have ACKed */
	
	sigemptyset(&newmask);
	sigaddset(&newmask, SIGALRM);

	bzero(timer, sizeof(*timer));
	
	//initialize rtt info
	rtt->srtt = 0;
	rtt->rttvar = 3000;
	rtt->rto = rtt_minmax(3000);
	rtt->windowPing = rtt_minmax(1500);

	
	FD_ZERO(&fdset);

	bzero(&childaddr, sizeof(childaddr));
	childaddr.sin_family = AF_INET;

	bzero(&localaddr, sizeof(localaddr));
	localaddr.sin_family = AF_INET;

	local_size = sizeof(localaddr);

	printf("Child spawned to handle request for file: %s\n", requstedFile);

	/* Close all the sockets in this child except the one that received the connection */
	for(i = 0; i < num_interfaces; i++){
		if(i != index){
			close(if_list[i].fd);
		}
	}

	/* Determine if the client is on the same local network */
	for(i = 0; i < num_interfaces; i++){
		struct sockaddr_in *intaddr = (struct sockaddr_in *) if_list[i].ifi_addr;
		uint32_t uintaddr = intaddr->sin_addr.s_addr; 	/* pull address as integer from structure */

		struct sockaddr_in *intmask = (struct sockaddr_in *) if_list[i].ifi_ntmaddr;
		uint32_t uintmask = intmask->sin_addr.s_addr;	/* pull address as integer from structure */

		/* mask the addresses */
		uint32_t uintsubnet_addr = uintaddr & uintmask;
		uint32_t uintserver_addr = (cliaddr->sin_addr.s_addr) & uintmask;
		//if the XOR is = 0, then perfect match, and therefore local
		if( (uintsubnet_addr ^ uintserver_addr) == 0){
			isLocal = 1;
			break;
		}
	}

	/* if the client is on the local net, then use the SO_DONTROUTE socket option */
	if(isLocal == 1){
		printf("*Client is in local network\n");
		if(setsockopt(if_list[index].fd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on)) < 0){
			printf("error setting socket option: %s\n", strerror(errno));
			exit(1);
		}
	}else{
		printf("*Client is not in local network\n");
	}

	/* create the connection socket */
	connfd = socket(AF_INET, SOCK_DGRAM, 0);
	setsockopt(connfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	/* set the family and allow the port to be ephemeral */
	//NOTE: had to make this new child address structure so we weren't overwriting the server's port
	childaddr.sin_addr.s_addr = (((struct sockaddr_in *) if_list[index].ifi_addr)->sin_addr.s_addr);
	childaddr.sin_port = htons(0);

	/* Bind the the child connection socket on ephemeral port */
	if(bind(connfd, (SA *)&childaddr, sizeof(childaddr)) < 0){
		printf("error on bind: %s\n",strerror(errno));
		exit(1);		
	}else{
		printf("*Socket bound for child to clinet communication ");
	}

	/* get the ip and port server is bound to for this client */
	if(getsockname(connfd, (SA *)&localaddr, &local_size) < 0){
		printf("error :%s\n",strerror(errno));
		exit(1);
	}else{
		printf("on interface %s with port: %i\n", 
			sock_ntop_host((struct sockaddr *)&localaddr, sizeof(localaddr)),
			ntohs(localaddr.sin_port));
		/* set the servers port to this variable */
		serverEPort = ntohs(localaddr.sin_port);
	}

	/* connect to the client (cliaddr) and print status */
	printf("*Client port number: %i\n", ntohs(cliaddr->sin_port));
	printf("*Client address: %s\n", sock_ntop_host((struct sockaddr *) cliaddr, sizeof(cliaddr)));
	if(connect(connfd, (SA *)cliaddr, sizeof(*cliaddr)) < 0){
		printf("error connecting to client %s with error %s\n", 
			sock_ntop_host((SA *) cliaddr, sizeof(*cliaddr)), strerror(errno));
		exit(1);
	}else{
		printf("Successfully connected to client %s!\n\n",	
			sock_ntop_host((SA *) cliaddr, sizeof(*cliaddr)));
	}

	int sendPort = 1;
	while(1){
		FD_SET(pipefd, &fdset);
		FD_SET(connfd, &fdset);
		maxfdp1 = max(pipefd,connfd)+1;

		//Set the timer for the receive
		selectWaitTime.tv_sec = 5L;
    	selectWaitTime.tv_usec = 0L;
		
		/* put the server's port into the buffer */
		sprintf(body,"%i",htons(serverEPort));
		/* build the header */
		receive_header.ack = htonl(1);
		receive_header.seq_num = htonl(0);
		receive_header.receive_window = htonl(0);
		/* put everything into the datagram */
		memcpy(&datagram, &receive_header, sizeof(receive_header));
		memcpy(&datagram[HEADER_SIZE], &body, strlen(body)+1);

		/*send on both the original and new port - one will just get ignored and one will be accepted */
		if(sendPort){
			if(sendto(connfd, datagram, (HEADER_SIZE+strlen(body)), 0, NULL, sizeof(*cliaddr)) < 0){ //was: (SA *)cliaddr
				printf("error sending %s\n", strerror(errno));
			}else{
				printf("sent port number %i to client on ephemeral port\n", serverEPort);
			}
			
			//send on the original port
			if(sendto(if_list[index].fd, datagram, (HEADER_SIZE+strlen(body)), 0, (SA *)cliaddr, sizeof(*cliaddr)) < 0){ //
				printf("error sending %s\n", strerror(errno));
			}else{
				printf("sent port number %i to client on well-known port\n", serverEPort);
			}
		}else{
			//only skip sending once -- flag will be set next time if it needs to be
			sendPort = 1;
		}

		/* wait for an ack */
		//sit on a select - waiting from a pipe and a socket
		if(select(maxfdp1, &fdset, NULL, NULL, &selectWaitTime) < 0){
			printf("error with select: %s\n", strerror(errno));
			exit(1);
		}
		
		/* check pipe */
		char bufpipe[MAXLINE];
		if(FD_ISSET(pipefd, &fdset)){
			if(read(pipefd, bufpipe, MAXLINE) < 0){
				printf("error with select: %s\n", strerror(errno));
			}else{
				if(strlen(bufpipe) <= 0){
					printf("no message\n");
				}else{
					printf("The client requested the file again!\n");
					continue;
				}
			}
		}

		/* check socket */
		if(FD_ISSET(connfd, &fdset)){
			if(recvfrom(connfd, buf, MAXLINE, 0, NULL, &local_size) < 0){ //(SA *)cliaddr
				continue;

				if(errno == 111){
					sendPort = 0;
					//printf("received a bad connection packet\n");
					errno = 0;
					continue;
				}else{
					printf("error with rcv: %s\n", strerror(errno));
					exit(1);
				}
			}else{
				//Set the receiving window
				memcpy(&receive_header, &buf, sizeof(receive_header));
				receive_window = ntohl(receive_header.receive_window);
				//printf("Seq Number = %i, ", ntohl(receive_header.seq_num));
				//printf("ACK = %i, ", ntohl(receive_header.ack));
				//printf("Window size = %i\n", ntohl(receive_header.receive_window));
				printf("Acknowledge received. . . Starting file transfer.\n\n");
				break;
			}
		}
	}
	


	/* ----------- Begin sending the file ------------- */
	requestFile = fopen(requstedFile, "r");
	if(requestFile == NULL){
		printf("Error reading file!\n"); //TODO: send something nice back to the client
		exit(1);
	}

	//retransmit signal
	signal(SIGALRM, sendto_alarm);

	if(setitimer(ITIMER_REAL, timer, NULL) < 0){
		printf("set timer error :%s\n", strerror(errno));
		exit(1);
	}

	//try to send whatever another packet can be sent
	int last_byte = 0; /* flag for the last byte */
	while(1){

		/* If all packets are ACKed and there is no more to read then complete no time wait needed */
		if(packetsInFlight == 0 && last_byte == 1){
			printf("File done transferring - server terminating child process. . .\n");
			break;
		}

		//If there is a timeout
		if(sigsetjmp(jmpbuf, 1) != 0){
			//printf("Timer Expired!\n");
			//if there are no packets in flight do a window ping - breaks window deadlock
			if(packetsInFlight == 0){
				printf("*Window deadlock detected!\n");
				//create the special window request packet:
				//create the header
				struct header send_header;
				send_header.ack = htonl(0);
				send_header.seq_num = htonl(0);
				send_header.receive_window = htonl(0);
				send_header.size = htonl(0);
				send_header.fin = htonl(0);

				//create the packet
				struct sent_packet *dgram_packet = malloc( sizeof(struct sent_packet) );
				bzero(dgram_packet, sizeof(*dgram_packet));
				dgram_packet->next = NULL;
				dgram_packet->packet_header = send_header;
				bzero(&(dgram_packet->body), 512);
				dgram_packet->acks = 0;
				dgram_packet->times_sent = 0;
				dgram_packet->ts_msec = get_msec();

				//send the packet
				send_packet(dgram_packet, connfd, &windowHead, cliaddr, &timer, &rtt);

				printf("*Sent out window probing packet. . .\n");

				//update the window ping timer
				rtt->windowPing = rtt_minmax(rtt->windowPing*2);
				timer->it_value.tv_usec = (rtt->windowPing * 1000);

			//else resend head packet
			}else{
				/* crash the congestion window */
				ssthresh = (int)ceil((float)cwnd/(float)2.0);
				cwnd = 1;
				ackCntr = 0;

				//check if already transmitted the maximum number of times
				if(windowHead != NULL && windowHead->times_sent > RTT_MAXNREXMT){
					printf("*Droping connection - tried to send %i times and failed", windowHead->times_sent);
					return;
				}
				//double the retransmit timeout and resend
				printf("Packet #%i timed out - resending (ssthresh=%i)!\n", ntohl(windowHead->packet_header.seq_num), ssthresh);
				rtt->rto = rtt_minmax(rtt->rto*2);
				send_packet(windowHead, connfd, &windowHead, cliaddr, &timer, &rtt);
			}
		}

		/* for as long as there are packets to be sent and sending window to use, send packets */
		do{
			//mask the alarm
			sigprocmask(SIG_BLOCK, &newmask, NULL);
			bytesRead = 0;
			//read only if packets in flight is less than the congestion window and room in receiving window
			if(packetsInFlight<cwnd && receive_window>0){
				//only read more in if there is more to read
				if(last_byte==0 && (bytesRead = fread(bufFile, 1, sizeof(bufFile), requestFile)) < 512){
					printf("\nLAST BYTE:");
					//the number of bytes read this time is less than the buf, set the last packet
					last_byte = 1;
				}
			}
			/* Send the packet if there is data and space in window */
			if(bytesRead > 0){
				printf("\nRead %i bytes from file.\n", bytesRead);
				//first packet of file will be seq # 1
				packetNumber++;

				//create the header
				struct header send_header;
				send_header.ack = htonl(0);
				send_header.seq_num = htonl(packetNumber);
				send_header.receive_window = htonl(0);
				send_header.size = htonl(bytesRead);
				send_header.fin = htonl(last_byte);

				//create the packet
				struct sent_packet *dgram_packet = malloc( sizeof(struct sent_packet) );
				bzero(dgram_packet, sizeof(*dgram_packet));
				dgram_packet->next = NULL;
				dgram_packet->packet_header = send_header;
				memcpy(&(dgram_packet->body), &bufFile, 512);
				dgram_packet->acks = 0;
				dgram_packet->times_sent = 0;
				dgram_packet->ts_msec = get_msec();

				//add and send the packet
				add_packet(dgram_packet, &windowHead, &packetsInFlight);
				send_packet(dgram_packet, connfd, &windowHead, cliaddr, &timer, &rtt);
			}
			//unmask the alarm
			sigprocmask(SIG_UNBLOCK, &newmask, NULL);
		}while(bytesRead > 0 && packetsInFlight < cwnd); 

		//read one ACK from the socket
		if(recvfrom(connfd, buf, MAXLINE, 0, (SA *)cliaddr, &local_size) < 0){
			printf("Receive from error: %s\n", strerror(errno));
		}else{
			printf("\n\njust got a packet from the client\n");
			memcpy(&receive_header, &buf, sizeof(receive_header));
			
			//if this is an ack, process the ack and update the window
			if(ntohl(receive_header.ack) == 1){
				receive_window = ntohl(receive_header.receive_window);

				//print out a message saying that the receive buffer is full	
				if(receive_window == 0){
					printf("Receiver window is 'locked'!\n");
				}

				printf("Client ACKed with seq_num %i\n", ntohl(receive_header.seq_num));

				//call the process acks - pass in this ACKed sequence number
				process_acks(ntohl(receive_header.seq_num), &windowHead, &timer, &rtt, &packetsInFlight, connfd, cliaddr, &cwnd, &ssthresh, &ackCntr, &rwind_init);
				
				//if processing acks result in nothing left - clear timeout
				timer->it_value.tv_usec = 0;
			}
		}
	}
}



/* ---------- LINKED LIST FUNCTIONS ---------- */
/* check if the node exists, and add if it doesn't */
struct linked_list *is_client(uint32_t ip, uint32_t port ){
	struct linked_list *current;
	if(head==NULL){
		//was not found in the list, return 0	
		return NULL;
	}else{
		current = head;
		while(current != NULL){
			if(current->ip==ip && current->port==port){
				//was found in the list, return 1
				return current;
			}
			current=current->next;	
		}
	}
	//was not found in the list, return 0
	return NULL;
}

/* add a new node, if it does not exsit in the linked list*/
void add_client(uint32_t ip, uint32_t port, pid_t pid, int lepipe){
	struct linked_list *current=head;
	struct linked_list *new_node;
	new_node = (struct linked_list*) malloc( sizeof(struct linked_list) );
	new_node->ip=ip;
	new_node->port=port;
	new_node->pid=pid;
	new_node->pipefd = lepipe;
	new_node->next = NULL;

	if(head==NULL)
	{
		//set the head - was not found in the list, return 0
		head = new_node;	
	}
	else
	{
		while(current->next!=NULL)
		{
			current=current->next;	
		}
		current->next = new_node;
	}
	return;
}

void 	remove_client(uint32_t ip, uint32_t port)
{
	struct linked_list *current=head;
	if(head==NULL){
		//if the head is null, it means no such node in the list
		return;	
	}else{
		if(head->ip==ip && head->port==port){
			head=head->next;
		}else{

			while(current->next!=NULL){
				if(head->ip==ip && head->port==port){
					current->next=current->next->next;
					return;
				}else{
					current=current->next;	
				}
			}
		}
	}
	return;
}


void remove_client_pid(pid_t pid)
{
	struct linked_list *current=head;
	if(head==NULL){
		//if the head is null, it means no such node in the list
		return;	
	}else{
		if(head->pid==pid){
			head=head->next;
		}else{

			while(current->next!=NULL){
				if(head->pid==pid){
					current->next=current->next->next;
					return;
				}else{
					current=current->next;	
				}
			}
		}
	}
	return;
}



/* ---------- SIGNAL FUNCTIONS ---------- */
void sig_child(int signo){
	pid_t pid;
	int stat;

	while( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
		remove_client_pid(pid);
	}
	return;
}
static void sendto_alarm(int signo)
{
	siglongjmp(jmpbuf, 1);
}

/* ---------- WINDOW FUNCTIONS ---------- */
/* process everything UP TO BUT NOT INCLUDING the ACK as received */
void process_acks(int seq_num, struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt, unsigned int *packetsInFlight, int connfd, struct sockaddr_in *cliaddr, int *cwnd, int *ssthresh, int *ackCntr, int *rwind_init){
	struct sent_packet *current = *windowHead;
	//go through and remove the packets that are less than the seq_num
	while(current != NULL){
		if(ntohl(current->packet_header.seq_num) < seq_num){
			/* This packet has been ACKed, update the congestion window */
			if(*cwnd < *ssthresh){
				//if under the threshold, do doubling window
				*cwnd = min(*cwnd + 1, *rwind_init); /* increment the congestion window, but don't go past max window */
				printf("Packet Acknowledge during Slow Start - New cwnd=%i\n", *cwnd);
			}else{
				//add one to the cwnd every nth time if over threshold 
				//(where n is the number of packets in flight at time of last update)
				if(*ackCntr != 0 && *ackCntr%*cwnd == 0){
					*ackCntr = 0;							/* put the counter back to zero */
					*cwnd = min(*cwnd + 1, *rwind_init);	/* increment the congestion window, but don't go past max window */
					printf("Set of packets acknowledged after slow start - New cwnd=%i\n", *cwnd);
				}else{
					*ackCntr = *ackCntr + 1; /* received 1/n packets */
				}
			}

			//the ack that was received covers this one, so it can be removed
			if(current->acks < 4){
				unsigned long long int newTs = get_msec();
				newTs = newTs - (current->ts_msec);
				do_rtt((unsigned long) newTs, rtt);
			}
			printf("Removing packet #%i from sending window because received ACK for %i.\n",
				ntohl(current->packet_header.seq_num), seq_num);
			remove_packet(ntohl(current->packet_header.seq_num), windowHead, timer, rtt, packetsInFlight);
			current = *windowHead;
		}else if(ntohl(current->packet_header.seq_num) == seq_num){
			//this is the next one the client is expecting to receive, increment ACK count
			if( (current->acks = (current->acks+1)) == 4){
				/* Update the congestion window for fast retransmit */
				//note that fast retransmit does not experience slow start
				*ssthresh = (int) ceil((float)*cwnd / (float)2.0);
				*cwnd = *ssthresh;
				*ackCntr = 0;

				/* fast retransmit */
				printf("Received 3 duplicate ACKs for packet #%i, doing Fast Retransmit! (cwnd=ssthresh=%i)\n", ntohl(current->packet_header.seq_num), *ssthresh);
				//if got 3 duplicate acks (total of 4 acks), retransmit and reset timer
				send_packet(current, connfd, windowHead, cliaddr, timer, rtt);
			}
			current = current->next;
		}else{
			//passed everything of interest
			break;
		}
	}
	return;
}

/* removes the specified packet number (by seq number) from the list */
void remove_packet(int seq_num, struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt, unsigned int *packetsInFlight){
	struct sent_packet *current = *windowHead;
	*packetsInFlight = *packetsInFlight - 1;
	if(*windowHead==NULL){
		return;
	}else if(ntohl((*windowHead)->packet_header.seq_num) == seq_num){
		*windowHead = (*windowHead)->next;
		reset_head_timer(windowHead, timer, rtt);
		return;
	}else{
		//should never need to remove something besides head - so this is here just for completeness
		while(current->next != NULL){
			if(ntohl(current->next->packet_header.seq_num) == seq_num){
				current->next=current->next->next;
				break;
			}else{
				current=current->next;	
			}
		}
		return;
	}
}

/* adds the packet to the linked list */
void add_packet(struct sent_packet *dgram_packet, struct sent_packet **windowHead, unsigned int *packetsInFlight){
	struct sent_packet *current = *windowHead;
	*packetsInFlight = *packetsInFlight + 1;
	if(*windowHead == NULL){
		*windowHead = dgram_packet;
		current = *windowHead;
	}else{
		while(current->next != NULL){
			current = current->next;
		}
		current->next = dgram_packet;
	}
	printf("Added packet #%i to sending window (%i packets in window).\n", ntohl(dgram_packet->packet_header.seq_num), *packetsInFlight);
	return;
}

/* sends the packet to the client */
void send_packet(struct sent_packet *dgram_packet, int connfd, struct sent_packet **windowHead, struct sockaddr_in *cliaddr, struct itimerval **timer, struct rtt_info **rtt){
	//increment the times sent
	dgram_packet->times_sent = dgram_packet->times_sent + 1;

	//print window info
	printf("Sending window starts with packet #%i ", ntohl((*windowHead)->packet_header.seq_num));
	struct sent_packet *current = *windowHead;
	while(current->next!=NULL){
		current = current->next;
	}
	printf("ends with packet %i.\n", ntohl(current->packet_header.seq_num));

	//send the packet
	printf("Sending packet #%i (# times sent = %i).\n", ntohl(dgram_packet->packet_header.seq_num), dgram_packet->times_sent);

	//if sending the first one, update timeout
	if(ntohl((*windowHead)->packet_header.seq_num) == ntohl(dgram_packet->packet_header.seq_num)){
		reset_head_timer(windowHead, timer, rtt);
	}

	//datagram to send
	char datagram[MAXLINE];

	//copy the header and body into the datagram
	memcpy(&datagram, &(dgram_packet->packet_header), sizeof(dgram_packet->packet_header));
	memcpy(&datagram[HEADER_SIZE], &dgram_packet->body, ntohl(dgram_packet->packet_header.size));

	//send the packet to the client
	if(sendto(connfd, datagram, HEADER_SIZE+512, 0, NULL, sizeof(*cliaddr)) < 0 ){ //was (SA *)cliaddr
		printf("error in sendto %s\n", strerror(errno));
	}
	return;
}

/* returns the msec since epoc */
unsigned long long int get_msec(){
    struct timeval tv;
	gettimeofday(&tv, NULL);
	unsigned long long int msecTimeStamp = (unsigned long long int)((unsigned long long int)tv.tv_usec)/1000;
	msecTimeStamp += (unsigned long long int) ((unsigned long long int)tv.tv_sec * 1000);
	return msecTimeStamp;
}

/* calculate the rtt based on the dif (time between send and receive) and the rtt info */
void do_rtt(unsigned long dif, struct rtt_info **rtt){
	int delta = (dif - ((*rtt)->srtt >> 3));
	(*rtt)->srtt = (*rtt)->srtt + delta;
	(*rtt)->rttvar = (*rtt)->rttvar + (abs(delta)-((*rtt)->rttvar));
	(*rtt)->rto = rtt_minmax(((*rtt)->srtt >> 3) + (*rtt)->rttvar);
	printf("new rtt info in msec: rto=%i rtvar=%i srtt=%i\n", (*rtt)->rto, ((*rtt)->rttvar >> 2), ((*rtt)->srtt >> 3));	
	return;
}

/* pass through the min max function when setting the new RTO */
int rtt_minmax(int rtt){
	if(rtt<RTT_RXTMIN){
		return RTT_RXTMIN;
	}else if(rtt>RTT_RXTMAX){
		return RTT_RXTMAX;
	}else{
		return rtt;
	}
}

/* resets the timer for the longest outstanding packet in flight */
void reset_head_timer(struct sent_packet **windowHead, struct itimerval **timer, struct rtt_info **rtt){
	//set the window ping timeout to be 1.5 seconds (1500msec)
	(*rtt)->windowPing = 1500;
	//only set the timer when the window head is null
	if(*windowHead != NULL){
		//transform the rto from msec to usec
		struct itimerval timernew;
		bzero(&timernew, sizeof(struct itimerval));
		int sec_part = ((*rtt)->rto * 1000)/1000000;
		int usec_part = ((*rtt)->rto * 1000)%1000000;
		timernew.it_value.tv_sec = sec_part;
		timernew.it_value.tv_usec = usec_part;

		//printf("timer: rto=%i, tv_usec=%i, tv_sec=%i\n", (*rtt)->rto, timernew.it_value.tv_usec, timernew.it_value.tv_sec);

		if(setitimer(ITIMER_REAL, &timernew, NULL)==-1)
		{
			printf("set timer error :%s\n", strerror(errno));
			exit(1);
		}

		printf("Reset the timeout for head packet to RTO=%imsec.\n", (*rtt)->rto);	
	}else{
		//if nothing in the window - just clear the timer
		struct itimerval timernew;
		bzero(&timernew, sizeof(struct itimerval));
		timernew.it_value.tv_sec = 0;
		timernew.it_value.tv_usec = 0;
		if(setitimer(ITIMER_REAL, &timernew, NULL)==-1)
		{
			printf("set timer error :%s\n", strerror(errno));
			exit(1);
		}
	}
}

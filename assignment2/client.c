#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/file.h>
#include <math.h>
#include <setjmp.h>
#include "unp.h"
#include "unpthread.h"
#include "unpifiplus.h"
#include "sys/time.h"

struct client_interface
{
	struct sockaddr *ifi_addr;
	struct sockaddr *ifi_ntmaddr;
	int intformat_ip;
	int is_local;
};

struct header
{
	uint32_t  			seq_num;		/* this is really packet number */
	uint32_t 			ack;			/* set to 1 to have ack flag on */
	uint32_t 			fin;			/* fin flag - used to show last packet in transfer */
	uint32_t 			receive_window;	/* the receive window space at this point */
	uint32_t			size;			/* size of the body*/
};

struct package
{
	struct package  	*next;
	struct header  		packageheader;
	char 				packagebody[MAXLINE-20];	
};

struct thread
{
	int 				 mean;
};

pthread_mutex_t		ndone_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t   	ndone_cond = PTHREAD_COND_INITIALIZER;

/*window*/
uint32_t 			*window_count;
uint32_t			*window_ads;
uint32_t			*nextExpSeq;	
struct 	package 	*window_header = NULL;

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
static int drop(float lost_prob);
static void sendto_alarm(int signo);
static void window_count_receive(struct package *receive_package, int receive_window);
static void update_nextExpseq(struct package *receive_package);
static void *consume_thread(void * newthread);
static void printoutfile();


int main (int argc, char ** argv)
{
	static const char 			clientConfig []= "client.in";
	FILE 						*configFile;
	char				 		buff[MAXLINE];
	char 						ip[MAXLINE];
	uint32_t 					port=0;
	char 						filename[MAXLINE];
	uint32_t 					receive_window=0;
	int 						seed;
	float 						lost_prob = 0;
	int 						mean = 0;
	int 						time_wait = 0;
	char						recvline[MAXLINE];
	int 						m;						//send byte
	int 						n;						//receive byte
	char						datagram[MAXLINE];
	char						body[512];
	char 						header_datagram[20];
	char 						receive_body[MAXLINE-20];
	int 						r;
	socklen_t 					len;
	socklen_t 					local_size;
	socklen_t 					peer_size;

	struct ifi_info *ifi;
	struct sockaddr_in serveraddr;
	struct sockaddr_in clientaddr;
	struct sockaddr_in permlocaladdr;
	struct sockaddr_in localaddr;
	struct sockaddr_in peeraddr;
	struct sockaddr_in newserveraddr;
	struct sockaddr_in newclientaddr;
	struct sockaddr_in newlocaladdr;
	struct itimerval timer;
	int  	num_interface=0;
	int 	count=0;
	int 	i=1;

	uint32_t uintaddr = 0;
	uint32_t uintmask = 0;
	uint32_t uintsubnet_addr;
	uint32_t uintserver_addr;

	/*Socket File Descriptor*/
	int 			sockfd;
	int 			sockfdnew;
	const int 		on = 1;
	int  			sockflag = 0; 

	/*Thread Variables*/
	pthread_t		 tid;



	uint32_t nextExpSeq_space = 1;
	nextExpSeq = &nextExpSeq_space;

	uint32_t window_count_space;
	window_count = &window_count_space;

	uint32_t window_ads_space = 0;
	window_ads = &window_ads_space;

	bzero(&permlocaladdr, sizeof(permlocaladdr));
	permlocaladdr.sin_family = AF_INET;
	inet_pton(AF_INET,"127.0.0.1",&permlocaladdr.sin_addr);

	bzero(&localaddr, sizeof(localaddr));
	localaddr.sin_family = AF_INET;

	bzero(&peeraddr, sizeof(peeraddr));
	peeraddr.sin_family = AF_INET;
	
	bzero(&newserveraddr, sizeof(newserveraddr));
	newserveraddr.sin_family=AF_INET;
	
	bzero(&newclientaddr, sizeof(newclientaddr));
	newclientaddr.sin_family=AF_INET;

	bzero(&newlocaladdr, sizeof(newlocaladdr));
	newlocaladdr.sin_family=AF_INET;		

	/* Read Parameter from client.in file */
	configFile=fopen(clientConfig,"r");
	if(configFile!=NULL)
	{
		printf("\n====Start Of Config File====\n");
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("ip address is not defined in 'client.in' file\n");
				exit(1);
			}
			strcpy(ip,buff);
			printf("Server IP: %s",ip);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("port number is not defined in 'client.in' file\n");
				exit(1);
			}
			port = atoi(buff);
			printf("Port Number: %i\n",port);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("file name is not defined in 'client.in' file \n");
				exit(1);
			}
			strcpy(filename,buff);
			printf("File Name: %s",filename);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("Max receive window is not defined in 'client.in' file\n");
				exit(1);
			}
			(*window_ads) = receive_window = atoi(buff);
			printf("Max receive_window: %i\n",receive_window);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("seed is not defined in 'client.in' file\n");
				exit(1);
			}
			seed = atoi(buff);
			srand(seed);
			printf("Random Seed: %i\n",seed);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("loss probability is not defined in 'client.in' file\n");
				exit(1);
			}
			lost_prob = atof(buff);
			printf("Loss Probability: %f\n",lost_prob);
		}
		if(fgets(buff, sizeof(buff), configFile)!=NULL)
		{
			if(strlen(buff)<1)
			{
				printf("mean is not defined in 'client.in' file\n");
				exit(1);
			}
			mean = atoi(buff);
			printf("Mean: %i\n", mean);
		}
		printf("====End Of Config File====\n\n");
	}
	else
	{
		printf("Error reading client config file!\n");
		exit(1);
	}
	/* End of Config File */


	/* Get IP Address and Associated Network Mask */
	for(ifi = Get_ifi_info_plus(AF_INET,1);ifi!=NULL;ifi = ifi->ifi_next)
	{
		num_interface++;
	}
	struct client_interface ifi_list[num_interface];
	for(ifi=Get_ifi_info_plus(AF_INET,1);ifi!=NULL;ifi=ifi->ifi_next)
	{
		ifi_list[count].ifi_addr = ifi->ifi_addr;
		ifi_list[count].ifi_ntmaddr = ifi->ifi_ntmaddr;
		ifi_list[count].is_local = 0; 

		printf("\nInterface %s:\n", ifi->ifi_name);
		printf("IP Address:%s\n", sock_ntop_host(ifi_list[count].ifi_addr, sizeof(*ifi_list[count].ifi_addr)));
		printf("Network Mask:%s\n", sock_ntop_host(ifi_list[count].ifi_ntmaddr, sizeof(*ifi_list[count].ifi_ntmaddr)));


		struct sockaddr_in *intaddr = (struct sockaddr_in *) ifi_list[count].ifi_addr;
		uintaddr = intaddr->sin_addr.s_addr;
		struct sockaddr_in *intmask = (struct sockaddr_in *) ifi_list[count].ifi_ntmaddr;
		uintmask = intmask->sin_addr.s_addr;

		ifi_list[count].intformat_ip = uintaddr;
		count++;
	}
	printf("\n");

	/* Check if the Server on the Same Host */
	for(i=0;i<count;i++)
	{
		if(ip[(strlen(ip)-1)]=='\n')
		{
			ip[(strlen(ip)-1)]='\0';
		}
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_port = htons(port);
		inet_pton(AF_INET,ip,&serveraddr.sin_addr);
		//printf("Server Address From input is: %i\n",serveraddr.sin_addr.s_addr);
		//printf("Client Address From function is: %i\n", ifi_list[i].intformat_ip);

		if(serveraddr.sin_addr.s_addr==ifi_list[i].intformat_ip)
		{
			sprintf(ip,"%s","127.0.0.1");
			//client address is also going to be 127.0.0.1
			inet_pton(AF_INET,ip,&serveraddr.sin_addr);
			printf("Interface %i's IP Address is on the same host as server, using (%s)\n",i,ip);
			break;
		}
		else
		{
			printf("Interface %i's IP Address is not on the same host as server, using (%s)\n",i,ip);
			//is it on the same local area network
			uintsubnet_addr = uintaddr & uintmask;
			uintserver_addr = (serveraddr.sin_addr.s_addr) & uintmask;
			if( (uintsubnet_addr ^ uintserver_addr) == 0){
				//save as a possibility
				ifi_list[i].is_local=1;
			}
		}
	}

	/* Decide which interface to use */
	uint32_t largestMask = 0;
	int largestMaskIndex = 0;
	int onLocal = 0;
	for(i=0; i<count; i++){
		if(ifi_list[i].is_local == 1){
			largestMask = max(largestMask, ((struct sockaddr_in *) ifi_list[i].ifi_ntmaddr)->sin_addr.s_addr);
			largestMaskIndex = i;
			onLocal=1;
		}
	}

	//If the largest mask is set, then use the largestMaskIndex address as the client's address
	bzero(&clientaddr, sizeof(clientaddr));
	clientaddr.sin_family = AF_INET;
	if(onLocal == 1){
		clientaddr.sin_addr.s_addr = ((struct sockaddr_in *) ifi_list[largestMaskIndex].ifi_addr)->sin_addr.s_addr; // set client address
	}else{ // if not local pick arbitrary one that is not 127.0.0.1 
		for(i=0; i<count; i++){
			if(((struct sockaddr_in *) ifi_list[i].ifi_addr)->sin_addr.s_addr != permlocaladdr.sin_addr.s_addr){
				clientaddr.sin_addr.s_addr = ((struct sockaddr_in *) ifi_list[i].ifi_addr)->sin_addr.s_addr; // set client address
			}
		}
	}

	//print selected client and server address:
	if(onLocal == 0){
		printf("*Server is not on local network\n");
		if(permlocaladdr.sin_addr.s_addr == serveraddr.sin_addr.s_addr){
			printf("*Server and client are on same host\n");
		}
	}else{
		printf("*Server is on local network\n");
	}
	printf("*Selected client ip: %s\n", sock_ntop_host((struct sockaddr *) &clientaddr, sizeof(clientaddr)));
	printf("*Selected server ip: %s\n", sock_ntop_host((struct sockaddr *) &serveraddr, sizeof(serveraddr)));


	/* Client creat a UDP socket */
	if((sockfd=socket(AF_INET,SOCK_DGRAM,0)) < 0){
		printf("Error creating socket %s\n", strerror(errno));
	}
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)) < 0 ){
		printf("Error setting socket option %s\n", strerror(errno));	
	}
	clientaddr.sin_port = htons(0);
	if(bind(sockfd, (SA *)&clientaddr, sizeof(clientaddr)) == 0)
	{
		local_size = sizeof(localaddr);
		peer_size = sizeof(peeraddr);
	}
	else
	{
		printf("Bind error: %s\n",strerror(errno));
		exit(1);
	}

	/* Get local ephemeral port number */
	if(getsockname(sockfd, (SA *)&localaddr,&local_size) < 0)
	{
		printf("get sock name error: %s\n",strerror(errno));
		exit(1);
	}
	printf("Assigned local Port Number:%i\n",localaddr.sin_port);

	/* make connection to server parent in preparation to send the filename */
	if(connect(sockfd, (SA *)&serveraddr, sizeof(serveraddr)) == -1)
	{
		printf("connect error :%s\n",strerror(errno));
		exit(1);
	}

	/* get the server's address/port */
	if(getpeername(sockfd, (SA *)&peeraddr, &peer_size) < 0)
	{
		printf("get peer name error is %s\n", strerror(errno));
		exit(1);
	}
	printf("Server IP Address : %s\n", sock_ntop_host((struct sockaddr *) &peeraddr, sizeof(peeraddr)));
	printf("Server Port Number: %i\n", ntohs(peeraddr.sin_port));

//===================================Send a Datagram of filename========================================//
	/*send the file name in the datagram body*/
	struct header client_header;
	struct thread newthread;
	
	/*do some cleanup on the filename */
	if(filename[strlen(filename)-1]=='\n'){
		filename[strlen(filename)-1]='\0';
	}
	strcpy(body,filename);
	
	/*Initialize the header structure*/
	client_header.ack = htonl(0);
	client_header.seq_num = htonl(0);
	client_header.receive_window = htonl(receive_window);
	client_header.size = htonl(strlen(filename));
	//printf("window=%i seqnum=%i\n", ntohl(client_header.receive_window), ntohl(client_header.seq_num));

	/*creat the datagram*/
	memcpy(&datagram, &client_header, sizeof(client_header));
	memcpy(&datagram[20], &body, strlen(body));
	
	/* Set time out action */
	signal(SIGALRM, sendto_alarm);

	/*Create a new thread for consumer*/
	newthread.mean = mean;
	if(pthread_create(&tid, NULL, &consume_thread, &newthread) < 0)
	{
		printf("Create new thread error :%s\n", strerror(errno));
		exit(1);
	}

	/* REMOVED Send File Request if first time or a timeout */
	//if(sockflag == 0 || sigsetjmp(jmpbuf, 1) != 0){
	//	
	//}
	
	/* Receive messages and handle appropriately */
	while(1)
	{
		//printf("sock flag value = %i\n", sockflag);
		/* try to get port from the server's parent */
		if(sockflag == 0)
		{
			/* Send the first datagram */
			m = sendto(sockfd, datagram, (20+strlen(body)), 0, NULL, sizeof(serveraddr)); //(SA *)&serveraddr
			if(m == -1)
			{
				printf("send to error is: %s\n", strerror(errno));
				exit(1);
			}else{
				printf("\nSent File Request message to server with filename=%s, window=%i, seqnum=%i\n", body,
					ntohl(client_header.receive_window), ntohl(client_header.seq_num));
			}
			len = sizeof(serveraddr);

			/* time for resending the File Request */
			timer.it_value.tv_sec = 5;
			timer.it_value.tv_usec = 0;
			if(setitimer(ITIMER_REAL, &timer, NULL)==-1)
			{
				printf("set timer error :%s\n", strerror(errno));
				exit(1);
			}
			//printf("before receive");
			if((n = recvfrom(sockfd, recvline, MAXLINE, 0, (struct sockaddr *) &serveraddr, &len)) < 0)
		 	{
				if(errno==EINTR)
				{	
					continue;
				}
				else
				{
					printf("read error is: %s error number :%i\n", strerror(errno),errno);
					exit(1);
				}
			}
			//printf("after receive");
		}

		/* Try to get something from the server's child */
		if(sockflag == 1)
		{
			//printf("Waiting for data from server's child process\n");
			//printf("new sock Descriptor :%i\n", sockfdnew);
			//printf("server child port : %i IP : %s\n", ntohs(newserveraddr.sin_port), sock_ntop_host((struct sockaddr *) &newserveraddr, sizeof(newserveraddr)));
			

			/* time wait state */
			if(time_wait == 1){
				//read something, so clear
				timer.it_value.tv_sec = 5;
				timer.it_value.tv_usec = 0;
				if(setitimer(ITIMER_REAL, &timer, NULL)==-1)
				{
					printf("set timer error :%s\n", strerror(errno));
					exit(1);
				}
			}

			if((n = recvfrom(sockfdnew, recvline, MAXLINE, 0, (struct sockaddr *)&newserveraddr, &len)) <= 0)
		 	{
				if(errno==EINTR)
				{
					if(time_wait == 1){
						printf("\n\nFile Transfer Complete! Shutting Down Client!\n");
						exit(1);
					}
					continue;
				}
				else
				{
					printf("read error is: %s error number :%i\n", strerror(errno),errno);
					exit(1);
				}
			}

			if(time_wait == 1){
				//read something, so clear
				timer.it_value.tv_sec = 0;
				timer.it_value.tv_usec = 0;
				if(setitimer(ITIMER_REAL, &timer, NULL)==-1)
				{
					printf("set timer error :%s\n", strerror(errno));
					exit(1);
				}
			}
		}
		if(drop(lost_prob) == 0)
		{
			printf("Droped the received Packet!\n\n");
			continue;
		}
		else
		{	
			/* process the packet received */
			struct package	*receive_package = (struct package*) malloc( sizeof(struct package) );
			struct header	receive_header;	
			memcpy(&receive_header, &recvline, sizeof(receive_header));
			memcpy(&receive_package->packagebody, &recvline[20], 512);
			receive_package->next = NULL;
			receive_package->packageheader = receive_header;
			printf("\nPacket Received (seq_num=%i, ack=%i)\n",ntohl(receive_header.seq_num),ntohl(receive_header.ack));
			
			/* logic for last packet */
			if(ntohl(receive_header.fin) == 1){
				printf("*This is the last packet from the server!\n");
				time_wait = 1;
			}
			
			/*check the sequence number if it is 0, it means client received the server's port number*/
			if(ntohl(receive_header.seq_num) == 0)
			{

				/* turn off the timer */
				timer.it_value.tv_sec = 0;
				timer.it_value.tv_usec = 0;
				if(setitimer(ITIMER_REAL, &timer, NULL) < 0)
				{
					printf("set timer error :%s\n", strerror(errno));
					exit(1);
				}

				/* get the port number */
				memcpy(&receive_body, &recvline[20],(MAXLINE-20));
				newserveraddr.sin_port = atoi(receive_body);
				newserveraddr.sin_addr.s_addr = serveraddr.sin_addr.s_addr;
				printf("Server's child process is on port %i\n",ntohs(newserveraddr.sin_port));

				/* Create a struct for the package and Initialize it */
				memcpy(&receive_package->packagebody, &receive_body, 512);	

				/* Client creat a new UDP socket */
				if((sockfdnew = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
				{
					printf("Error creating new socket %s", strerror(errno));
					exit(1);
				}

				/* set the new sock option */
				if(setsockopt(sockfdnew, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
				{
					printf("Error setting new socket option %s\n", strerror(errno));
					exit(1);
				}

				/* create new client address */
				newclientaddr.sin_port = localaddr.sin_port;
				newclientaddr.sin_addr.s_addr = localaddr.sin_addr.s_addr;
				if(bind(sockfdnew, (SA *)&newclientaddr, sizeof(newclientaddr)) < 0)
				{
					printf("Error binding new socket: %s\n", strerror(errno));
					exit(1);
				}

				/* connect to the server child's address and port */
				if(connect((sockfdnew), (SA *)&newserveraddr, sizeof(newserveraddr)) < 0)
				{
					printf("Error with connecting to server's child: %s\n", strerror(errno));
					exit(1);	
				}
				else
				{
					/* Set the sock flag to indicate that we now have a connection to server's child */
					sockflag = 1;
					//for testing
					printf("New bind is on client port number: %i\n", ntohs(newclientaddr.sin_port));
				}
			}
			
			/* if sequence number is not 0, it is file data */
			if(ntohl(receive_header.seq_num) != 0)
			{
				//printf("Data packet received:\n(can we get the lock? \n");
				/*CHECK THE IF IT IS LOCKED*/
				if(pthread_mutex_lock(&ndone_mutex) != 0)
				{
					printf("Lock error : %s\n", strerror(errno));
					exit(1);
				};
					//printf("yes!)\n");
					//ADD TO THE WINDOW
					window_count_receive(receive_package, receive_window);

				/*UNLOCK IT WHEN IT IS DONE*/
				if(pthread_mutex_unlock(&ndone_mutex) != 0)
				{
					printf("Unlock error :%s\n", strerror(errno));
					exit(1);
				}
				//printf("Data package Unlock!!!\n");
			}

			/* Send ACK to the server for this packet (regardles what type it is) */
			if(drop(lost_prob) == 0)
			{
				/*drop this ack*/
				continue;
			}
			else
			{
				/* send the ack */
				//setup header
				client_header.ack = htonl(1);
				client_header.receive_window = htonl(*window_ads); //set the advertised window size
				client_header.seq_num = htonl(*nextExpSeq); //the next packet number we are expecting
				client_header.size = htonl(0); //empty body
				memcpy(&header_datagram, &client_header, sizeof(client_header));
				//send packet
				r = sendto(sockfdnew, header_datagram, sizeof(client_header), 0, NULL, sizeof(newserveraddr)); //was (SA *)&newserveraddr
				if(r == -1)
				{
					printf("Send ack error is :%s\n", strerror(errno)); 
					exit(1);
				}
				else
				{
					printf("Sent ACK with seq number %i\n", *nextExpSeq);
				}
			}
		}
	}

	/*waiting for thread ends*/
	//pthread_join(tid, NULL);
	return 0;
}

/*Set up the alarm*/
static void sendto_alarm(int signo)
{
	return;
	//siglongjmp(jmpbuf, 1);
}

/**
 * Check if should drop packet
 * Returns 0 if should drop, 1 if should keep 
 */
static int drop(float lost_prob)
{
	float			actual_drop_prob;
	actual_drop_prob = (double)rand() / (double)RAND_MAX;
	//printf("\nrand=%f, loss prob=%f\n",actual_drop_prob, lost_prob);
	if(actual_drop_prob < lost_prob){
		return 0;
	}else{
		return 1;
	}
}

/*Create produce part for the window*/
static void window_count_receive(struct package *receive_package, int receive_window)
{
	struct package *current;
	current = window_header;

	/* if window advertised space is zero, say window is full */
	if(*window_ads == 0){
		printf("*The window is full now!!!\n");
	}

	//print status of window
	printf("Adding sequence number %i to the window.\n", ntohl(receive_package->packageheader.seq_num));

	/* Only add to the window if we get a sequance number that is greater or equal to the next expected
		if we get a sequence number that is less than NE, don't add to window - means we already got this one
		(we will still ACK it) */
	if(ntohl(receive_package->packageheader.seq_num) >= *nextExpSeq){
		*window_count = *window_count + 1; //adding one to the window
		*window_ads = receive_window - *window_count; //recalculate how much space to advertise

		/* update the window with the receive_package */
		if(window_header == NULL){
			window_header = receive_package;
			//printf("Setting up the first window head,sequence number = %i\n", ntohl(window_header->packageheader.seq_num));
		}else{
			//decide where the receive_package needs to go
			if(ntohl(receive_package->packageheader.seq_num) < ntohl(window_header->packageheader.seq_num)){
				receive_package->next = window_header;
				window_header = receive_package;
				//printf("Received packet with sequence = %i (less than head) add to the window head\n",ntohl(receive_package->packageheader.seq_num));
			}else{
				while(current->next != NULL){
					if(ntohl(receive_package->packageheader.seq_num) > ntohl(current->next->packageheader.seq_num)){
						current = current->next;
					}else{
						break;
					}
				}
				//place new packet after current one
				receive_package->next = current->next;
				current->next = receive_package;
				//printf("We add packet with sequence = %i into a gap or at the end of the window \n",ntohl(receive_package->packageheader.seq_num));
			}
		}
	}

	//printf("should call update next expected: nextExpSeq=%i, thisone=%i\n", *nextExpSeq,ntohl(receive_package->packageheader.seq_num));
	/* Update the next expected when filling in a gap in the window */
	if(*nextExpSeq == ntohl(receive_package->packageheader.seq_num)){
		update_nextExpseq(receive_package);
	}

	return;
}

/* Update the Next Expected Sequence Number */
static void update_nextExpseq(struct package *receive_package)
{
	struct package *current;
	current = window_header;

	//this is only run when adding a packet with the next expected sequence number.
	//threfore head will have less than or equal to sequence number, but not more

	/* find the node before the gap or before the end of the list */
	while(current->next != NULL && ntohl(current->next->packageheader.seq_num) == (ntohl(current->packageheader.seq_num)+1))
	{
		current = current->next;
	}

	/* Update next expected sequence number */
	*nextExpSeq = ntohl(current->packageheader.seq_num) + 1;
	printf("Send this next expected Sequence Number: %i as ACk\n", *nextExpSeq);		
}

/* Create consume thread for the window */
static void *consume_thread(void *arg)
{
	struct thread * newthread = (struct thread *) arg;
	struct timeval  	tv;
	struct timespec 	ts;
	struct package 		*current;
	float 				sleeptime;

	while(1)
	{	
		
		/* Set the time to sleep */
		if(gettimeofday(&tv, NULL) < 0)
		{
			printf("Consumer: Error in get day time :%s\n", strerror(errno));
			exit(1);
		}

		sleeptime = (-1) * (newthread->mean) * log((double)rand() / (double)RAND_MAX);
		//printf("rand fraction %f\n", (double)rand() / (double)RAND_MAX);
		//printf("Sleep time= %i\n",sleeptime);
		ts.tv_sec  = tv.tv_sec;
    	ts.tv_nsec = tv.tv_usec * 1000;
    	ts.tv_sec += ceil(sleeptime);
		//printf("double %f or RAND_MAX=%i ", ((float)rand() / (float)RAND_MAX), RAND_MAX);
		//printf("Consumer: Sleep time nsec: %li\n", ts.tv_nsec);

		/* have the thread wait until the sleep time has occured */
		if(pthread_mutex_lock(&ndone_mutex) != 0)
		{
			printf("Consumer: Lock error : %s\n", strerror(errno));
			exit(1);
		}
		pthread_cond_timedwait(&ndone_cond, &ndone_mutex, &ts);
			//printf("after wait time :%i\n", ndone_mutex);
		/*CHECK THE IF IT IS LOCKED*/
		current = window_header;
		//printf("Consumer: acquired lock.\n\n");
		if(current == NULL)
		{
			printf("Consumer: No thing in the window to read!!\n");
		}
		while(current != NULL)
		{
			if(ntohl(current->packageheader.seq_num) < *(nextExpSeq))
			{
				/*print the content of package in the stdout*/
				printf("Consumer: Printing contents of packet number %i\n", ntohl(current->packageheader.seq_num));
				printoutfile();
				window_header = current->next;
				current = window_header;
			}
			else
			{
				break;
			}	
		}
		if(pthread_mutex_unlock(&ndone_mutex) != 0)
		{
			printf("Consumer: Unlock error: %s\n", strerror(errno));
			exit(1);
		} 
		//printf("Consumer: released lock\n");
	}
	return NULL;
}

static void printoutfile(){
	printf("%s\n\n", window_header->packagebody);
	fflush(stdout);
	return;
}

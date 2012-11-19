#include "unp.h"

int main(int argc, char **argv)
{
	int udpfd;
	fd_set rset;
	int maxfdp1;
	int nready;
	struct sockaddr_in 				servaddr;
    struct timeval 					timeOut;
	
	bzero(&servaddr, sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(9998);
	inet_pton(AF_INET,"127.0.0.1",&servaddr.sin_addr);

	//TCP Works fine
	udpfd = socket(AF_INET, SOCK_STREAM, 0);
	//UDP Does not work
	//udpfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpfd < 0){
		printf("sock creation error");
	}

	bind(udpfd, (SA *) &servaddr, sizeof(servaddr));

	FD_ZERO(&rset);
	maxfdp1 = udpfd +1;
	FD_SET(udpfd, &rset);

	timeOut.tv_sec = 2L;
    timeOut.tv_usec = 0L;

	if((nready = select(maxfdp1, &rset, NULL, NULL, &timeOut)) < 0){
		printf("\nSELECT ERORR\n");
	}else{
		printf("\nOKAY\n");
	}
	return 0;
}

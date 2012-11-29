//
//  echo.c
//  seraphy2
//
//  Created by Mengli Yuan on 10/1/12.
//  Copyright (c) 2012 Mengli Yuan. All rights reserved.
//

#include "unp.h"
#define SIZE 1024

void str_cli(FILE *fp, int sockfd);
int main(int argc, char *argv[])
{
    int sockfd;
    struct  sockaddr_in  servaddr;
    
    printf("Into the child process of Echo Service.\n");
    printf("IP Address is %s \n",argv[1]);
    
    int fd;
    char testbuf[MAXLINE];
    int writeresult;
    
    fd=atoi(argv[2]);
    //printf("fd %i\n",fd);
    //printf("argv %s\n",argv[2]);
    sprintf(testbuf,"%s","hello\n");
    //printf("test : %s\n",testbuf);
    //writeresult=write(fd,testbuf,strlen(testbuf));
    //printf("writeresult value %i\n",writeresult);
    if(writeresult==-1)
    {
        printf("error: %s\n", strerror(errno));
    }
    else
    {
        printf("pipe information back to parents\n");
    }
   
    sockfd=socket(AF_INET,SOCK_STREAM,0);
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
	//servaddr.sin_port = htons(SERV_PORT);
    servaddr.sin_port = htons(9027);
	Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    if (connect(sockfd, (SA *) &servaddr, sizeof(servaddr))==-1)
    {
        printf("error: %s", strerror(errno));
    }
	str_cli(stdin, sockfd);
    sleep(20);
    exit(0);
   
    return 0;
}
void str_cli(FILE *fp,int sockfd)
{
    int maxfdp1;
    fd_set rset;
    char sendline[MAXLINE], recvline[MAXLINE];
    
    FD_ZERO(&rset);
    
    for (; ; ) {
        FD_SET(fileno(fp),&rset);
        FD_SET(sockfd,&rset);
        maxfdp1=max(fileno(fp),sockfd)+1;
        select(maxfdp1,&rset,NULL,NULL,NULL);
        
        if(FD_ISSET(sockfd,&rset))
        {
       
            if (readline(sockfd, recvline, MAXLINE) == 0)
                err_quit("\nstr_cli: server terminated prematurely");
                fputs(recvline, stdout);
        }
       if(FD_ISSET(fileno(fp),&rset))
        {
            if(fgets(sendline,MAXLINE,fp)==NULL)
            {
                return;
            }
            else
            {
                if ( writen(sockfd,sendline,strlen(sendline))==-1) {
                    printf("error: %s \n", strerror(errno));
                }
            }
        }
    }
}


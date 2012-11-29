//
//  time.c
//  
//
//  Created by Mengli Yuan on 10/2/12.
//
//

#include <stdio.h>
#include "unp.h"

int main(int argc, char *argv[])
{
    int  sockfd, n;
    char recvline[MAXLINE+1];
    struct sockaddr_in servaddr;
    printf("Into the child process of Time Service\n");
    printf("Time Service will print the current time every 5 seconds\n");
    printf("IP Address is %s \n",argv[1]);
    
    if(argc!=2)
    {
        err_quit("usage : a.out <IPadddress>\n");
    }
    sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd<0)    
    {
        printf("error : %s \n",strerror(errno));
    }
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    //servaddr.sin_port=htons(13);
    servaddr.sin_port=htons(9037);
    if (inet_pton(AF_INET,argv[1],&servaddr.sin_addr)<=0)
    {
        err_quit("inet_pton error for %s \n",argv[1]);
    }
    if(connect(sockfd,(SA *)&servaddr,sizeof(servaddr))<0)
    {
        printf("error : %s\n", strerror(errno));
       // err_sys("connect error\n");
    }
    for( ; ; )
    {
        n=read(sockfd,recvline,MAXLINE);
        if(n>0)
        {
            recvline[n]=0;
            if(fputs(recvline,stdout)==EOF)
            {
                err_sys("fputs error\n");
            }
        }
        if (n<0)
        {
            err_sys("read error\n");
        }
    }
       
}

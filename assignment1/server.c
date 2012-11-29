//
//  server.c
//  seraphy2
//
//  Created by Mengli Yuan on 10/5/12.
//  Copyright (c) 2012 Mengli Yuan. All rights reserved.
//

#include <stdio.h>
#include "unp.h"
#include "unpthread.h"

static void *doit_echo(void *arg);
static void *doit_time(void *arg);
int main(int argc, char *argv[])
{
    //define 2 socket file descriptor, listenfd1-->time,listenfd2-->echo
    int listenfd1,listenfd2; //connfd;
    int socketset_result1,socketset_result2,select_result;
    int on=1;
    int maxfd;
    struct sockaddr_in servaddr;
    fd_set rset;
    
    //time service part
    //char buff[MAXLINE];
    //time_t ticks;
    
    //echo service part
    socklen_t addrlen,len;
    struct sockaddr_in *cliaddr;
    
    //thread variables 
    int *iptr;
    pthread_t tid;
    
    
    
    //set the socket to listen socket

    
    listenfd1=socket(AF_INET,SOCK_STREAM,0);
    listenfd2=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd1<0||listenfd2<0)
    {
        printf("error: %s \n", strerror(errno));
        exit(-1);
    }
    else
    {
        printf("server socket is good\n");
    }
    
    //set the socket to reusable
    socketset_result1=
    setsockopt(listenfd1,SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));
    socketset_result2=
    setsockopt(listenfd2,SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));
    if(socketset_result1<0)
    {
        printf("set socket1 failed\n");
        close(listenfd1);
        exit(-1);
    }
    if(socketset_result2<0)
    {
        printf("set socket2 failed\n");
        close(listenfd2);
        exit(-1);
    }
    else
    {
        printf("set socket is good\n");
    }
    
    //bind the socket1
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    //servaddr.sin_port=htons(13);
    servaddr.sin_port=htons(9037);
    socketset_result1=bind(listenfd1,(SA *)&servaddr,sizeof(servaddr));
    if(socketset_result1<0)
    {
        printf("bind socket1 failed\n");
        close(listenfd1);
        exit(-1);
    }
    else
    {
        printf("bind socket1 is good\n");
    }
    
    //bind the socket2
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    //servaddr.sin_port=htons(SERV_PORT);
    servaddr.sin_port=htons(9027);
    socketset_result1=bind(listenfd2,(SA *)&servaddr,sizeof(servaddr));
    if(socketset_result2<0)
    {
        printf("bind socket2 failed\n");
        close(listenfd2);
        exit(-1);
    }
    else
    {
        printf("bind socket2 is good\n");
    }
    
    //set the listen to the socket number
    socketset_result1=listen(listenfd1,LISTENQ);
    socketset_result2=listen(listenfd2,LISTENQ);
    if(socketset_result1<0)
    {
        printf("listen1 failed\n");
        close(listenfd1);
        exit(-1);
    }
    if(socketset_result2<0)
    {
        printf("listen2 failed\n");
        close(listenfd2);
        exit(-1);
    }
    else
    {
        printf("sever is ready for the connection\n");
    }
    
    
    //Initialize
    FD_ZERO(&rset);
    for (; ; )
    {
        FD_SET(listenfd1,&rset);
        FD_SET(listenfd2,&rset);
        maxfd=max(listenfd1,listenfd2)+1;
        
        printf("Into select function\n");
        select_result=select((maxfd+1),&rset,NULL,NULL,NULL);
        if(select_result<0)
        {
            printf("there is an error in select\n");
            exit(1);
        }
        else
        {
            printf("select good\n");
            if(FD_ISSET(listenfd1,&rset))
            {
                printf("time service now\n");
                {
                    iptr=malloc(sizeof(int));
                    *iptr=accept(listenfd1,(SA *)NULL,NULL);
                    if(pthread_create(&tid,NULL,&doit_time,iptr)==0)
                    {
                        printf("new thread created\n");
                    }
                    else
                    {
                        printf("new thread failed\n");
                    }
                }
            }
            if(FD_ISSET(listenfd2,&rset))
            {
                printf("echo service now\n");
                cliaddr=malloc(addrlen);
                {
                    len=addrlen;
                    iptr=malloc(sizeof(int));
                    *iptr=accept(listenfd2,cliaddr,&len);
                    if(pthread_create(&tid,NULL,&doit_echo,iptr)==0)
                    {
                        printf("new thread created\n");
                    }
                    else
                    {
                        printf("new thread failed\n");
                    }
                }
            }
        }
    }


}
static void *doit_echo(void *arg)
{
    int connfd;
    
    connfd=*((int *) arg);
    free(arg);
    
    pthread_detach(pthread_self());
    str_echo(connfd);
    close(connfd);
    return (NULL);
}
static void *doit_time(void *arg)
{
    int connfd;
    char buff[MAXLINE];
    time_t ticks;
    
    
    connfd=*((int *)arg);
    free(arg);
    
    pthread_detach(pthread_self());
    
    if(connfd==-1)
    {
        printf("error: %s \n", strerror(errno));
        sleep(5);
    }
    if(connfd>=0)
    {
        for( ; ; )
        {
            ticks=time(NULL);
            snprintf(buff, sizeof(buff), "%.24s\r\n",ctime(&ticks));
            write(connfd,buff,strlen(buff));
            sleep(5);
        }
    }
    return(NULL);
}




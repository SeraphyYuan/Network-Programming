
//
//  hostent2.c
//  seraphy2
//
//  Created by Mengli Yuan on 9/24/12.
//  Copyright (c) 2012 Mengli Yuan. All rights reserved.
//


#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include "unp.h"
#define SIZE 1024

int main(int argc, char *argv[])
{
    //IP part
    struct hostent *hptr;
    struct in_addr addr;
    socklen_t len;
    int type;
    int version_check=0;
    int result=0;
    char **pptr;
    char str[INET_ADDRSTRLEN];
    char servicebuf[MAXLINE];
    
    //child variable
    //pipe variable
    pid_t pid, childpid;
    int stat;
    int pfd[2];
    int readresult;
    char buf[SIZE];
    char buf2[SIZE];
    
    if(argc==2)
    {
        hptr=gethostbyname(argv[1]);
        if (hptr==NULL) {
            //err_msg("gethostbyname error for host: %s: %s",argv[1],hstrerror(h_errno));
            printf("host name is not exist\n");
        }
        else
        {
            printf("Official Host %s\n", hptr->h_name);
            for (pptr=hptr->h_aliases; *pptr!=NULL; pptr++) {
                printf("\talias : %s\n",*pptr);
            }
            if (hptr->h_addrtype==AF_INET)
            {
                pptr=hptr->h_addr_list;
                for (; *pptr!=NULL; pptr++)
                {
                    printf("\taddress : %s\n",inet_ntop(AF_INET, *pptr, str, sizeof(str)));
                }
            }
            else
            {
                // err_ret("unknow address type");
                printf("unknow address type\n");
            }
        }
    }
    else if (argc==3)
    {
        if (strcasecmp(argv[2], "af_inet")==0) {
            type=AF_INET;
            len=4;
            version_check=0;
        }
        else if(strcasecmp(argv[2],"af_inet6")==0)
        {
            type=AF_INET6;
            len=16;
            version_check=1;
        }
        else
        {
            printf("type wrong format in cmd\n");
            //return 0;
        }
        if (version_check==0) {
            addr.s_addr=inet_addr(argv[1]);
            hptr=gethostbyaddr((char*)&addr, len, type);
        }
        if (version_check==1) {
            result=inet_pton(AF_INET6, argv[1], &addr);
            if (result==0) {
                printf("the ipv6 addrss should be an legal address\n");
                //return 0;
            }
            else
            {
                hptr=gethostbyaddr((char*)&addr, len, type);
            }
        }
        printf("host : %s length %d\n",argv[1],len);
        printf("address type : %d\n", type);
        if(hptr==NULL)
        {
            printf("ip address is not exist\n");
            
        }
        if(hptr!=NULL)
        {
            printf("host name: %s\n", hptr->h_name);
        }
        //return 0;
    }
    else
    {
        printf("The format should be like:%s <IP_ADDRESS> <af_inet|af_inet6>\n",argv[0]);
        //return 0;
    }
    for (; ; )
    {
        printf("choose the service type: echo/time\n");
        if(gets(servicebuf)==NULL)
        {
            printf("error: %s\n", strerror(errno));
        }
        else
        {
            if(strcasecmp((servicebuf),"echo")==0)
            {
                childpid=fork();
                if(pipe(pfd)==-1)
                {
                    printf("pipe failed\n");
                    exit(1);
                }
                else
                {
                    printf("pipe successed\n");
                }
                if(childpid>=0)
                {
                    printf("fork successfully\n");
                    if(childpid==0)
                    {
                        printf("In echo child process,child pid : %d\n",childpid);
                        // child pipe part
                        close(pfd[0]);
                        sprintf(buf2,"%i",pfd[1]);
                        if ( (execlp("xterm", "xterm", "-e", "./echo2","130.245.1.44",buf2,(char *) 0)) < 0)
                        {
                            printf("error: %s \n", strerror(errno));
                            
                            exit(1);
                        }
                        close(pfd[1]);
                    }
                    if(childpid>0)
                    {
                        printf("in parent process\n");
                        //parent pipe part
                        close(pfd[1]);
                        readresult=read(pfd[0],buf2,SIZE);
                        if(readresult==-1)
                        {
                            printf("read error\n");
                            exit(1);
                        }
                        if(readresult==0)
                        {
                            //printf("no process has pipe back to parents\n");
                            //exit(1);
                        }
                        if(readresult!=-1&&readresult!=0)
                        {
                            printf("test %s\n",buf2);
                            close(pfd[0]);
                        }
                    }
                    pid=wait(&stat);
                    printf("child terminated\n");
                }
                if(childpid<0)
                {
                    printf("fork failed\n");
                }
            }
            else if(strcasecmp((servicebuf),"time")==0)
            {
                childpid=fork();
                if (pipe(pfd)==-1) {
                    printf("pipe failed");
                    exit(1);
                }
                if(childpid>=0)
                {
                    printf("fork successfully\n");
                    if(childpid==0)
                    {
                        printf("In time child process,child pid: %d\n",childpid);
                        if((execlp("xterm","xterm","-e","./time","130.245.1.44",(char *)0))<0)
                        {
                            printf("error: %s \n", strerror(errno));
                            exit(1);
                        }
                        pid=wait(&stat);
                        printf("child terminated");
                        
                    }
                    if(childpid>0)
                    {
                        printf("in parent process\n");
                    }
                    if(childpid<0)
                    {
                        printf("fork failed\n");
                    }
                }

            }
            else
            {
                printf("no such service\n");
            }

        }
        
    }
}





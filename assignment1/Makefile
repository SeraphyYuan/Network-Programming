CC = gcc

LIBS = -lresolv -lsocket -lnsl -lpthread\
	/home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a\
	
FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib

PROGS =	client\
		time\
		echo2\
		server
all: ${PROGS}


time: time.o
	${CC} ${FLAGS} -o time time.o ${LIBS}
time.o: time.c
	${CC} ${CFLAGS} -c time.c


echo2: echo2.o
	${CC} ${FLAGS} -o echo2 echo2.o ${LIBS}
echo2.o: echo2.c
	${CC} ${CFLAGS} -c echo2.c

client: client.o
	${CC} ${FLAGS} -o client client.o ${LIBS}
client.o: client.c
	${CC} ${CFLAGS} -c client.c


# server uses the thread-safe version of readline.c

server: readline.o server.o
	${CC} ${FLAGS} -o server readline.o server.o ${LIBS}
server.o: server.c
	${CC} ${CFLAGS} -c server.c	
	


# pick up the thread-safe version of readline.c from directory "threads"

readline.o: /home/courses/cse533/Stevens/unpv13e_solaris2.10/threads/readline.c
	${CC} ${CFLAGS} -c /home/courses/cse533/Stevens/unpv13e_solaris2.10/threads/readline.c


clean:
	rm echo2 echo2.o server server.o client client.o time time.o readline.o






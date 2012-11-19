include ../Make.defines

PROGS =	server client get_ifi_info_plus.o 

all:	${PROGS}

server:	server.o get_ifi_info_plus.o
		${CC} ${CFLAGS} -o $@ server.o get_ifi_info_plus.o ${LIBS} -lm

client:	client.o get_ifi_info_plus.o
		${CC} ${CFLAGS} -o $@ client.o get_ifi_info_plus.o ${LIBS} -lm		

get_ifi_info_plus.o: get_ifi_info_plus.c
		${CC} ${CFLAGS} -c get_ifi_info_plus.c ${LIBS}

clean:
		rm -f ${PROGS} ${CLEANFILES}

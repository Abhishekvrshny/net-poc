CC	= gcc
OPTFLAGS= -O2 -g $(EXTFLAGS)
CFLAGS	= -Wall -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 $(OPTFLAGS)
PROGS	= server client

all: $(PROGS)

server: server.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)
server.o:
		$(CC) src/server.c -c $(CFLAGS) $<
client: client.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^)
client.o:
		$(CC) src/client.c -c $(CFLAGS) $<
clean:
		-rm -f *.o .depend cscope.out $(PROGS) core.* core

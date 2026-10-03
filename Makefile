CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
LDLIBS  = -lrt

all: server client

server: server.c common.h
	$(CC) $(CFLAGS) -o $@ server.c $(LDLIBS)

client: client.c common.h
	$(CC) $(CFLAGS) -o $@ client.c $(LDLIBS)

clean:
	rm -f server client

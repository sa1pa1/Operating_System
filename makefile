CC = gcc
CFLAGS = -Wall -pthread

all: assignment3

assignment3: server.o
	$(CC) -o assignment3 server.o $(CFLAGS)

server.o: server.c
	$(CC) -c server.c $(CFLAGS)

clean:
	rm -f *.o assignment3

CC = gcc
CFLAGS = -Wall -Werror -std=gnu99 -pthread

all: dbserver dbclient clean

dbserver: dbserver.o
	$(CC) $(CFLAGS) -o $@ $^

dbclient: dbclient.o
	$(CC) $(CFLAGS) -o $@ $^

dbserver.o: dbserver.c msg.h
	$(CC) $(CFLAGS) -c $<

dbclient.o: dbclient.c msg.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f dbserver.o dbclient.o

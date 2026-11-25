CC     ?= gcc
CFLAGS ?= -Wall -Wextra -O2

all: interface socket

interface: interface.c
	$(CC) $(CFLAGS) -o $@ $<

socket: socket.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f interface socket

.PHONY: all clean

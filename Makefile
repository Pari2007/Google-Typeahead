CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
LDFLAGS ?=

TARGET = splay_server
OBJS = server.o splay.o trie.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

server.o: server.c splay.h trie.h
	$(CC) $(CFLAGS) -c server.c

splay.o: splay.c splay.h
	$(CC) $(CFLAGS) -c splay.c

trie.o: trie.c trie.h
	$(CC) $(CFLAGS) -c trie.c

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

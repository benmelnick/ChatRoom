SERVER_TARGET = chatserver 
CLIENT_TARGET = chatclient
LOG_TARGET = server_log.txt

CC     = gcc
CFLAGS = -Wall -Wextra -Wpointer-arith -Wshadow -Wpedantic -std=c11

LFLAGS = -pthread

SRCDIR = src
BINDIR = .

all: clean compile

.PHONY: clean
clean:
	@rm -f $(BINDIR)/$(SERVER_TARGET)
	@rm -f $(BINDIR)/$(CLIENT_TARGET)
	@rm -f $(LOG_TARGET)

.PHONY: compile
compile:
	@$(CC) $(CFLAGS) $(SRCDIR)/chatserver.c -o $(SERVER_TARGET) $(LFLAGS)
	@$(CC) $(CFLAGS) $(SRCDIR)/chatclient.c -o $(CLIENT_TARGET) $(LFLAGS)

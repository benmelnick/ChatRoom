SERVER_TARGET = server 
CLIENT_TARGET = client
LOG_TARGET = server_log.txt

CC     = gcc
CFLAGS = -Wall -Wextra -Wsign-conversion -Wpointer-arith -Wwrite-strings -Wshadow -Wpedantic -Wwrite-strings -std=c99

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
	@$(CC) $(CFLAGS) $(SRCDIR)/server.c -o $(SERVER_TARGET) $(LFLAGS)
	@$(CC) $(CFLAGS) $(SRCDIR)/client.c -o $(CLIENT_TARGET) $(LFLAGS)

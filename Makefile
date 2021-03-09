SERVER_TARGET = server 
CLIENT_TARGET = client
LOG_TARGET = server_log.txt

CC     = gcc
CFLAGS = -Wall -Wextra -Wsign-conversion -Wpointer-arith -Wwrite-strings -Wshadow -Wpedantic -Wwrite-strings -std=gnu99

LFLAGS = -lpthread

SRCDIR = src
INCDIR = $(SRCDIR)  # source and header files are both in the src directory
BINDIR = .

SRC := $(wildcard $(SRCDIR)/*.c)
INC := $(wildcard $(INCDIR)/*.h)

INCFLAGS := $(patsubst %/,-I%,$(dir $(wildcard $(INCDIR)/.)))

all: clean compile

.PHONY: clean
clean:
	@rm -f $(BINDIR)/$(SERVER_TARGET)
	@rm -f $(BINDIR)/$(CLIENT_TARGET)
	@rm -f $(LOG_TARGET)

.PHONY: compile
compile:
	@$(CC) $(CFLAGS) $(INCFLAGS) $(LFLAGS) $(SRCDIR)/server.c -o $(SERVER_TARGET)
	@$(CC) $(CFLAGS) $(INCFLAGS) $(LFLAGS) $(SRCDIR)/client.c -o $(CLIENT_TARGET)

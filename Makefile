# ============================================================
# Makefile – Chat Application v2.0 (Modular Build)
#
# Task 1.1: Multi-module build with strict warning flags.
# Targets: make all, make server, make client, make clean
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -g -O2 -I include
LDFLAGS = -lpthread

# Source files
COMMON_SRC   = src/common/protocol.c
SERVER_SRC   = src/server/main.c src/server/session.c src/server/router.c
CLIENT_SRC   = src/client/main.c src/client/net_handler.c src/client/ui_continuous.c

# Object files
COMMON_OBJ   = $(COMMON_SRC:.c=.o)
SERVER_OBJ   = $(SERVER_SRC:.c=.o)
CLIENT_OBJ   = $(CLIENT_SRC:.c=.o)

# Output binaries
SERVER_BIN   = server
CLIENT_BIN   = client

# ---- Build targets ----

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(COMMON_OBJ) $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(COMMON_OBJ) $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f $(COMMON_OBJ) $(SERVER_OBJ) $(CLIENT_OBJ)
	rm -f *.o

.PHONY: all clean server client

# Makefile for syslogd: configurable syslog server tools (C17, dependency-free).

CC ?= clang
CFLAGS ?= -std=c17 -Wall -Wextra -pedantic -O2
CFLAGS_DEBUG := -std=c17 -Wall -Wextra -pedantic -O0 -g
BUILD_DIR := build

SERVER := $(BUILD_DIR)/syslogd
CLIENT := $(BUILD_DIR)/syslogd_client
WEB    := $(BUILD_DIR)/syslogd_web

SERVER_SRC := src/syslogd.c
CLIENT_SRC := src/syslogd_client.c
WEB_SRC    := src/syslogd_web.c

ALL_TARGETS := $(SERVER) $(CLIENT) $(WEB)

.PHONY: all debug clean run

all: $(ALL_TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SERVER): $(SERVER_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC)

$(CLIENT): $(CLIENT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)

$(WEB): $(WEB_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(WEB_SRC)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG)" all

clean:
	rm -rf $(BUILD_DIR)

run: debug
	./$(SERVER) $(ARGS)
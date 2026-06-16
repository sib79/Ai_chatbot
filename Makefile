CC       = gcc
CFLAGS   = -std=c17 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS  = -pthread
SERVER_LIBS = -lcrypto

BUILD_DIR = build
SERVER_BIN = $(BUILD_DIR)/chat_server
CLIENT_BIN = $(BUILD_DIR)/chat_client
TEST_PROTOCOL_BIN = $(BUILD_DIR)/test_protocol
TEST_REGISTRY_BIN = $(BUILD_DIR)/test_registry
TEST_AUTH_BIN = $(BUILD_DIR)/test_auth

COMMON_SRCS = \
	src/common/protocol.c \
	src/common/message_queue.c \
	src/common/utils.c

SERVER_SRCS = \
	src/server/main.c \
	src/server/server.c \
	src/server/client_registry.c \
	src/server/broadcaster.c \
	src/server/auth.c \
	src/server/room_manager.c \
	src/server/commands.c

CLIENT_SRCS = \
	src/client/main.c \
	src/client/client.c \
	src/client/colors.c

COMMON_OBJS = $(COMMON_SRCS:src/%.c=$(BUILD_DIR)/%.o)
SERVER_OBJS = $(SERVER_SRCS:src/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJS = $(CLIENT_SRCS:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all server client clean test dirs

all: server client

dirs:
	@mkdir -p $(BUILD_DIR)/common $(BUILD_DIR)/server $(BUILD_DIR)/client

server: dirs $(SERVER_BIN)

client: dirs $(CLIENT_BIN)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVER_BIN): $(COMMON_OBJS) $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(SERVER_LIBS)

$(CLIENT_BIN): $(COMMON_OBJS) $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_PROTOCOL_BIN): tests/test_protocol.c $(BUILD_DIR)/common/protocol.o $(BUILD_DIR)/common/utils.o
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_protocol.c $(BUILD_DIR)/common/protocol.o $(BUILD_DIR)/common/utils.o $(LDFLAGS)

$(TEST_REGISTRY_BIN): tests/test_registry.c $(BUILD_DIR)/server/client_registry.o
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_registry.c $(BUILD_DIR)/server/client_registry.o $(LDFLAGS)

$(TEST_AUTH_BIN): tests/test_auth.c $(BUILD_DIR)/server/auth.o
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_auth.c $(BUILD_DIR)/server/auth.o $(LDFLAGS) $(SERVER_LIBS)

test: dirs $(TEST_PROTOCOL_BIN) $(TEST_REGISTRY_BIN) $(TEST_AUTH_BIN)
	@echo "Running protocol tests..."
	@$(TEST_PROTOCOL_BIN)
	@echo "Running registry tests..."
	@$(TEST_REGISTRY_BIN)
	@echo "Running auth tests..."
	@$(TEST_AUTH_BIN)

clean:
	rm -rf $(BUILD_DIR)

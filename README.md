# Real-Time Multi-User Chat Application (Advanced)

A production-style TCP/IP chat server and CLI client in C for Fedora Linux, featuring **authentication**, **chat rooms**, **private messaging**, **message history**, **admin controls**, and **colored terminal output**.

## Features

- SHA-256 password authentication (`data/users.db`)
- Chat rooms with room-scoped broadcasts (default: `lobby`)
- Private messages: `@user text` or `PM` protocol
- Online user list (`/users`)
- Per-room message history (last 100 messages, sent on join)
- Admin kick command (`/kick username`)
- ANSI colors on TTY (auto-disabled when piped)
- Up to 100 concurrent clients, thread-per-connection

## Requirements (Fedora)

```bash
sudo dnf install gcc make openssl-devel valgrind
```

## Build

```bash
cd OS-LAB-CHAT
make all      # build/chat_server, build/chat_client
make test     # unit tests
make clean
```

## User Database

File format (`data/users.db`):

```
username:sha256_hex_hash:admin_flag
```

| Field | Description |
|-------|-------------|
| username | Login name (max 31 chars) |
| sha256_hex_hash | Lowercase hex SHA-256 of password |
| admin_flag | `1` = admin, `0` = regular user |

Generate a hash:

```bash
echo -n "yourpassword" | sha256sum
```

### Default Test Accounts

| User | Password | Admin |
|------|----------|-------|
| admin | admin123 | yes |
| alice | pass | no |
| bob | pass | no |

Copy the example if needed:

```bash
cp data/users.db.example data/users.db
```

## Run

**Terminal 1 — Server**

```bash
./build/chat_server              # port 8080, data/users.db
./build/chat_server 9090         # custom port
./build/chat_server 8080 data/users.db
```

**Terminal 2+ — Clients**

```bash
./build/chat_client
./build/chat_client 127.0.0.1 8080 alice pass
./build/chat_client 127.0.0.1 8080 admin admin123
```

## Client Commands

| Input | Action |
|-------|--------|
| `message text` | Broadcast to current room |
| `@bob hello` | Private message to bob |
| `/users` | List all online users |
| `/join room1` | Switch to room1 (history auto-sent) |
| `/history` | Show current room history |
| `/kick alice` | Admin: disconnect alice |
| `/quit` | Leave server |

## Protocol Summary

**Client → Server:** `AUTH`, `JOIN_ROOM`, `MSG`, `PM`, `LIST`, `HISTORY`, `KICK`, `QUIT`

**Server → Client:** `WELCOME`, `ROOM_JOINED`, `BROADCAST`, `PM`, `USER_JOIN`, `USER_LEFT`, `HISTORY`, `KICKED`, `LIST`, `OK`, `ERR`

See [docs/DESIGN.md](docs/DESIGN.md) for architecture details.

## Valgrind

```bash
valgrind --leak-check=full ./build/chat_server
```

## Project Layout

```
OS-LAB-CHAT/
├── data/users.db          # User credentials (SHA-256)
├── include/               # Headers
├── src/server/            # Server + auth + rooms
├── src/client/            # Client + colors
├── src/common/            # Protocol, queue, utils
└── tests/                 # Unit tests
```

# NetMessenger - IE3010 Network Programming

Multi-client chat and file-sharing platform over TCP/IP, written in C with the
BSD sockets API.

## Personalised values (registration number IT23601956)

| Item | Value |
|---|---|
| Registration number | IT23601956 |
| Numeric part / last four / middle four | 23601956 / 1956 / 6019 |
| Port | 6000 + 1956 = **7956** |
| NID tag (appended to every OK/ERR line) | **NID:6019** |
| Source files | `server_1956.c`, `client_1956.c`, `Makefile_1956` |
| Log file | `netmsg_IT23601956.log` |
| Storage path | `./storage/IT23601956/<sender_username>/<filename>` |
| Archive | `IE3010_IT23601956.zip` |

## Build and run

    make -f Makefile_1956        # builds server_1956 and client_1956
    ./server_1956                # listens on 0.0.0.0:7956
    ./client_1956                # connects to 127.0.0.1:7956 (or: ./client_1956 <host> <port>)

Type protocol commands into the client, e.g. `REGISTER amal`, `LIST`,
`BCAST hello`, `PMSG nimal hi`, `JOIN room1`, `RMSG room1 hi`, `ROOMS`, `LEAVE room1`, `QUIT`.
For files use `SENDFILE <user-or-room> <local-path>`; the client computes the size and sends
`SENDFILE <target> <filename> <filesize>` followed by the raw bytes. Received files are saved
under `./downloads/<sender>/`.

## Run everything at once

    ./run_all.sh

Builds, checks that port 7956 is listening, runs all test suites in `tests/`, and prints the
log excerpt and the stored-file listing.

## Design summary

* Concurrency: one detached pthread per client; global mutex for user/room state and a
  per-client write mutex so lines and file streams never interleave.
* Framing: per-connection buffer, lines split on `\n` (partial lines and multiple lines per
  `recv()` handled); `SENDFILE` consumes exactly `<filesize>` bytes (leftover buffer first,
  then as many `recv()` calls as needed). Bytes are consumed even when the command is
  rejected, so the stream stays in sync.
* Error codes: 001 USERNAME_TAKEN, 002 USER_NOT_FOUND, 003 ROOM_NOT_FOUND, 004 FILE_TOO_LARGE,
  005 NOT_A_MEMBER, 006 NOT_REGISTERED, 007 UNKNOWN_COMMAND, 008 BAD_ARGUMENTS,
  009 ALREADY_REGISTERED, 010 TOO_MANY_ROOMS, 011 SERVER_FULL, 012 STORAGE_ERROR.
* Limits: 64 clients, 10 MB per file, 8 rooms per client. A room exists while it has members.

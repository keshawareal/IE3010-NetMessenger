# Design diary - NetMessenger (IT23601956)

Personalised values: port 7956, NID:6019, log netmsg_IT23601956.log,
storage ./storage/IT23601956/<sender>/<filename>.

## 2026-10-16 - decisions
* **Concurrency model: thread per client.** Simple to reason about, each client's blocking
  `recv()` is isolated, and 5+ simultaneous clients is far below what threads handle easily.
  Alternative considered: `select()/poll()` - single thread, no locking, but file transfers and
  partial-line state would need an explicit per-client state machine.
* **Shared state under one global mutex** (`gmu`: user list + rooms). A second mutex per
  client (`wmu`) serialises writes so a file stream and a chat line never interleave.
  Sends to other clients happen while holding `gmu`, so a client cannot be freed mid-send.
  Trade-off: a slow receiver can stall others; mitigated with a 10 s `SO_SNDTIMEO`.
* **Framing:** per-connection buffer; lines split on `\n`; handles half lines and several
  lines per `recv()`. SENDFILE uses the leftover buffer bytes first, then `recv()` until exactly
  `<filesize>` bytes are read.
* **Obstacle / decision - rejected files still consume their bytes.** If SENDFILE is rejected
  (unknown user, too large) the server still reads and discards `<filesize>` bytes; otherwise
  the file bytes would be parsed as commands.
* **Files are stored first, then forwarded** from disk, which satisfies the "server keeps a
  copy" requirement with one code path for user and room targets. A partial file is deleted if
  the sender disconnects mid-transfer.
* **Rooms:** a room exists while at least one client is a member (no separate room table).
* **Error codes:** brief fixes the format only, so I defined 001-012 (see README).
* **Assumption:** SENDFILE target is looked up as a username first, then as a room name.
* **Client:** two threads (stdin->socket, socket->screen) so incoming messages show while typing.
  Local helper `SENDFILE <target> <path>` computes the size and sends the exact protocol line.
* **Test obstacle:** a bare `wait` in the test scripts also waited for the background server
  and hung; fixed by `disown`ing the server in `tests/lib.sh`.

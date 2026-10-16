# Protocol notes (as implemented)

All responses to a client's command end with ` NID:6019`. Forwarded `MSG ...` lines do not.

| Command | Success | Errors |
|---|---|---|
| REGISTER u | OK REGISTERED u | 001, 008, 009 |
| LIST | OK USERS a,b | 006 |
| BCAST m | OK SENT | 006, 008 |
| PMSG u m | OK SENT | 002, 006, 008 |
| JOIN r | OK JOINED r | 006, 008, 010 |
| LEAVE r | OK LEFT r | 003 |
| ROOMS | OK ROOMS a,b | 006 |
| RMSG r m | OK SENT | 003, 005, 008 |
| SENDFILE t f n + n bytes | OK FILE_RECEIVED f | 002, 004, 005, 008 |
| QUIT | OK BYE | - |

Server-initiated lines: `MSG BCAST s m`, `MSG PRIV s m`, `MSG ROOM r s m`,
`MSG JOIN u`, `MSG LEAVE u`, and for files `MSG FILE s f n` followed by n raw bytes.
SENDFILE target resolution: a registered username first, then a room name.

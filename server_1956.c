/*
 * server_1956.c - NetMessenger server (IE3010 Network Programming)
 * Registration number : IT23601956
 * Port                : 6000 + 1956 = 7956
 * NID tag             : NID:6019
 * Log file            : netmsg_IT23601956.log
 * Storage path        : ./storage/IT23601956/<sender>/<filename>
 *
 * Concurrency model: one detached POSIX thread per client, shared state
 * protected by a global mutex (gmu) and a per-client write mutex (wmu).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LAST4        1956
#define PORT         (6000 + LAST4)          /* 7956 */
#define NID          "6019"                  /* digits 3-6 of 23601956 */
#define REGNO        "IT23601956"
#define LOGFILE      "netmsg_" REGNO ".log"
#define STORAGE_ROOT "./storage"
#define STORAGE_DIR  STORAGE_ROOT "/" REGNO

#define MAX_CLIENTS  64
#define MAX_ROOMS    8          /* rooms per client */
#define NAME_LEN     32
#define LINE_LEN     4096
#define MAX_FILE     (10ULL * 1024 * 1024)   /* 10 MB limit -> ERR 004 */

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    char name[NAME_LEN];
    int  registered;
    char rooms[MAX_ROOMS][NAME_LEN];
    int  nrooms;
    pthread_mutex_t wmu;                /* serialises writes to this socket */
} Client;

static Client *clients[MAX_CLIENTS];
static pthread_mutex_t gmu    = PTHREAD_MUTEX_INITIALIZER;  /* clients[] + rooms */
static pthread_mutex_t logmu  = PTHREAD_MUTEX_INITIALIZER;
static FILE *logfp;

/* ------------------------------------------------------------------ log */
static void logmsg(const char *fmt, ...)
{
    char ts[32], msg[1024];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&logmu);
    fprintf(logfp, "[%s] %s\n", ts, msg);
    fflush(logfp);
    pthread_mutex_unlock(&logmu);
    printf("[%s] %s\n", ts, msg);
    fflush(stdout);
}

/* ------------------------------------------------------------- sending */
static int send_all_locked(Client *c, const char *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(c->fd, data + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            shutdown(c->fd, SHUT_RDWR);      /* make its thread clean up */
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/* send one text line (adds '\n') */
static void send_line(Client *c, const char *fmt, ...)
{
    char buf[LINE_LEN + 128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n > sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    pthread_mutex_lock(&c->wmu);
    send_all_locked(c, buf, (size_t)n);
    pthread_mutex_unlock(&c->wmu);
}

/* OK/ERR responses: every one ends with " NID:6019" */
static void reply(Client *c, const char *fmt, ...)
{
    char buf[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    send_line(c, "%s NID:%s", buf, NID);
}

/* -------------------------------------------------- state (gmu held) */
static Client *find_by_name(const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i] && clients[i]->registered && !strcmp(clients[i]->name, name))
            return clients[i];
    return NULL;
}

static int in_room(const Client *c, const char *room)
{
    for (int i = 0; i < c->nrooms; i++)
        if (!strcmp(c->rooms[i], room)) return 1;
    return 0;
}

static int room_exists(const char *room)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i] && in_room(clients[i], room)) return 1;
    return 0;
}

static int valid_name(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= NAME_LEN) return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '_' && s[i] != '-') return 0;
    return 1;
}

static int valid_filename(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 200 || s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || (unsigned char)s[i] < 32) return 0;
    return 1;
}

/* ------------------------------------------------ file transfer helpers */
/* Read exactly `size` bytes: first from the leftover line buffer, then from
 * recv() as many times as needed. Writes to `out` if non-NULL (else drains). */
static int read_exact(Client *c, char *buf, size_t *blen,
                      unsigned long long size, FILE *out, int *werr)
{
    char tmp[8192];
    unsigned long long left = size;
    while (left > 0) {
        size_t n;
        if (*blen > 0) {
            n = *blen < left ? *blen : (size_t)left;
            memcpy(tmp, buf, n);
            memmove(buf, buf + n, *blen - n);
            *blen -= n;
        } else {
            size_t want = left < sizeof tmp ? (size_t)left : sizeof tmp;
            ssize_t r = recv(c->fd, tmp, want, 0);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return -1;              /* client went away */
            n = (size_t)r;
        }
        if (out && fwrite(tmp, 1, n, out) != n) *werr = 1;
        left -= n;
    }
    return 0;
}

static void send_file_to(Client *t, const char *sender, const char *fname,
                         unsigned long long size, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char hdr[LINE_LEN];
    int hn = snprintf(hdr, sizeof hdr, "MSG FILE %s %s %llu\n", sender, fname, size);
    pthread_mutex_lock(&t->wmu);
    if (send_all_locked(t, hdr, (size_t)hn) == 0) {
        char tmp[8192];
        size_t n;
        while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
            if (send_all_locked(t, tmp, n) < 0) break;
    }
    pthread_mutex_unlock(&t->wmu);
    fclose(f);
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* returns 0 = keep going, -1 = connection lost */
static int do_sendfile(Client *c, char *rest, char *buf, size_t *blen)
{
    char *sp = NULL;
    char *target = strtok_r(rest, " ", &sp);
    char *fname  = strtok_r(NULL, " ", &sp);
    char *szstr  = strtok_r(NULL, " ", &sp);
    char *extra  = strtok_r(NULL, " ", &sp);

    if (!target || !fname || !szstr || extra || strlen(szstr) > 15 ||
        strspn(szstr, "0123456789") != strlen(szstr)) {
        reply(c, "ERR 008 BAD_ARGUMENTS");
        return 0;
    }
    unsigned long long size = strtoull(szstr, NULL, 10);

    /* decide the outcome first, but ALWAYS consume the bytes so framing
     * stays correct for the next command */
    const char *err = NULL;
    int is_user = 0;
    if (!valid_filename(fname)) err = "ERR 008 BAD_FILENAME";
    else if (size > MAX_FILE)   err = "ERR 004 FILE_TOO_LARGE";
    else {
        pthread_mutex_lock(&gmu);
        if (find_by_name(target)) is_user = 1;
        else if (!room_exists(target)) err = "ERR 002 USER_NOT_FOUND";
        else if (!in_room(c, target))  err = "ERR 005 NOT_A_MEMBER";
        pthread_mutex_unlock(&gmu);
    }

    if (err) {
        int dummy = 0;
        if (read_exact(c, buf, blen, size, NULL, &dummy) < 0) return -1;
        logmsg("FILE_REJECTED from=%s target=%s file=%s size=%llu reason=%s",
               c->name, target, fname, size, err);
        reply(c, "%s", err);
        return 0;
    }

    char dir[512], path[800];
    snprintf(dir, sizeof dir, "%s/%s", STORAGE_DIR, c->name);
    mkdir_p(dir);
    snprintf(path, sizeof path, "%s/%s", dir, fname);
    FILE *out = fopen(path, "wb");
    int werr = (out == NULL);
    if (read_exact(c, buf, blen, size, out, &werr) < 0) {
        if (out) fclose(out);
        remove(path);                           /* discard partial file */
        logmsg("FILE_ABORTED from=%s file=%s (client disconnected)", c->name, fname);
        return -1;
    }
    if (out) fclose(out);
    if (werr) { reply(c, "ERR 012 STORAGE_ERROR"); return 0; }

    pthread_mutex_lock(&gmu);
    if (is_user) {
        Client *t = find_by_name(target);
        if (t) send_file_to(t, c->name, fname, size, path);
    } else {
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] && clients[i] != c && clients[i]->registered &&
                in_room(clients[i], target))
                send_file_to(clients[i], c->name, fname, size, path);
    }
    pthread_mutex_unlock(&gmu);

    logmsg("FILE from=%s target=%s file=%s size=%llu stored=%s",
           c->name, target, fname, size, path);
    reply(c, "OK FILE_RECEIVED %s", fname);
    return 0;
}

/* --------------------------------------------------- command handling */
/* returns 0 = continue, 1 = quit, -1 = connection lost */
static int handle_line(Client *c, char *line, char *buf, size_t *blen)
{
    char *cmd = line;
    char *rest = strchr(line, ' ');
    if (rest) *rest++ = 0; else rest = line + strlen(line);

    if (*cmd == 0) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }

    if (!strcasecmp(cmd, "QUIT")) {
        reply(c, "OK BYE");
        return 1;
    }

    if (!strcasecmp(cmd, "REGISTER")) {
        if (c->registered) { reply(c, "ERR 009 ALREADY_REGISTERED"); return 0; }
        if (!valid_name(rest)) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        pthread_mutex_lock(&gmu);
        if (find_by_name(rest)) {
            pthread_mutex_unlock(&gmu);
            reply(c, "ERR 001 USERNAME_TAKEN");
            return 0;
        }
        snprintf(c->name, sizeof c->name, "%s", rest);
        c->registered = 1;
        reply(c, "OK REGISTERED %s", c->name);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] && clients[i] != c && clients[i]->registered)
                send_line(clients[i], "MSG JOIN %s", c->name);
        pthread_mutex_unlock(&gmu);
        logmsg("REGISTER user=%s ip=%s", c->name, c->ip);
        return 0;
    }

    if (!c->registered) { reply(c, "ERR 006 NOT_REGISTERED"); return 0; }

    if (!strcasecmp(cmd, "LIST")) {
        char list[LINE_LEN - 64] = "";
        pthread_mutex_lock(&gmu);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] && clients[i]->registered) {
                if (*list) strncat(list, ",", sizeof list - strlen(list) - 1);
                strncat(list, clients[i]->name, sizeof list - strlen(list) - 1);
            }
        pthread_mutex_unlock(&gmu);
        reply(c, "OK USERS %s", list);
        return 0;
    }

    if (!strcasecmp(cmd, "BCAST")) {
        if (!*rest) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        pthread_mutex_lock(&gmu);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] && clients[i] != c && clients[i]->registered)
                send_line(clients[i], "MSG BCAST %s %s", c->name, rest);
        pthread_mutex_unlock(&gmu);
        logmsg("BCAST from=%s msg=%s", c->name, rest);
        reply(c, "OK SENT");
        return 0;
    }

    if (!strcasecmp(cmd, "PMSG")) {
        char *msg = strchr(rest, ' ');
        if (!*rest || !msg) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        *msg++ = 0;
        if (!*msg) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        pthread_mutex_lock(&gmu);
        Client *t = find_by_name(rest);
        if (t) send_line(t, "MSG PRIV %s %s", c->name, msg);
        pthread_mutex_unlock(&gmu);
        if (!t) { reply(c, "ERR 002 USER_NOT_FOUND"); return 0; }
        logmsg("PMSG from=%s to=%s msg=%s", c->name, rest, msg);
        reply(c, "OK SENT");
        return 0;
    }

    if (!strcasecmp(cmd, "JOIN")) {
        if (!valid_name(rest)) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        pthread_mutex_lock(&gmu);
        int full = 0;
        if (!in_room(c, rest)) {
            if (c->nrooms >= MAX_ROOMS) full = 1;
            else snprintf(c->rooms[c->nrooms++], NAME_LEN, "%s", rest);
        }
        pthread_mutex_unlock(&gmu);
        if (full) { reply(c, "ERR 010 TOO_MANY_ROOMS"); return 0; }
        logmsg("JOIN user=%s room=%s", c->name, rest);
        reply(c, "OK JOINED %s", rest);
        return 0;
    }

    if (!strcasecmp(cmd, "LEAVE")) {
        int found = 0;
        pthread_mutex_lock(&gmu);
        for (int i = 0; i < c->nrooms; i++)
            if (!strcmp(c->rooms[i], rest)) {
                memmove(c->rooms[i], c->rooms[i + 1],
                        (size_t)(c->nrooms - i - 1) * NAME_LEN);
                c->nrooms--;
                found = 1;
                break;
            }
        pthread_mutex_unlock(&gmu);
        if (!found) { reply(c, "ERR 003 ROOM_NOT_FOUND"); return 0; }
        logmsg("LEAVE user=%s room=%s", c->name, rest);
        reply(c, "OK LEFT %s", rest);
        return 0;
    }

    if (!strcasecmp(cmd, "ROOMS")) {
        char list[LINE_LEN - 64] = "";
        pthread_mutex_lock(&gmu);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) continue;
            for (int r = 0; r < clients[i]->nrooms; r++) {
                const char *rn = clients[i]->rooms[r];
                /* list each room once */
                int seen = 0;
                for (int j = 0; j < i && !seen; j++)
                    if (clients[j] && in_room(clients[j], rn)) seen = 1;
                if (seen) continue;
                if (*list) strncat(list, ",", sizeof list - strlen(list) - 1);
                strncat(list, rn, sizeof list - strlen(list) - 1);
            }
        }
        pthread_mutex_unlock(&gmu);
        reply(c, "OK ROOMS %s", list);
        return 0;
    }

    if (!strcasecmp(cmd, "RMSG")) {
        char *msg = strchr(rest, ' ');
        if (!*rest || !msg) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        *msg++ = 0;
        if (!*msg) { reply(c, "ERR 008 BAD_ARGUMENTS"); return 0; }
        const char *err = NULL;
        pthread_mutex_lock(&gmu);
        if (!room_exists(rest)) err = "ERR 003 ROOM_NOT_FOUND";
        else if (!in_room(c, rest)) err = "ERR 005 NOT_A_MEMBER";
        else
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (clients[i] && clients[i] != c && clients[i]->registered &&
                    in_room(clients[i], rest))
                    send_line(clients[i], "MSG ROOM %s %s %s", rest, c->name, msg);
        pthread_mutex_unlock(&gmu);
        if (err) { reply(c, "%s", err); return 0; }
        logmsg("RMSG from=%s room=%s msg=%s", c->name, rest, msg);
        reply(c, "OK SENT");
        return 0;
    }

    if (!strcasecmp(cmd, "SENDFILE"))
        return do_sendfile(c, rest, buf, blen);

    reply(c, "ERR 007 UNKNOWN_COMMAND");
    return 0;
}

/* ----------------------------------------------------- client thread */
static void *client_thread(void *arg)
{
    Client *c = arg;
    char buf[LINE_LEN];
    size_t blen = 0;
    int quit = 0;

    logmsg("CONNECT ip=%s fd=%d", c->ip, c->fd);

    while (!quit) {
        char *nl = memchr(buf, '\n', blen);
        if (!nl) {                               /* need more data */
            if (blen == sizeof buf) {            /* line too long: discard */
                reply(c, "ERR 008 LINE_TOO_LONG");
                blen = 0;
            }
            ssize_t n = recv(c->fd, buf + blen, sizeof buf - blen, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;                   /* EOF or error */
            blen += (size_t)n;
            continue;
        }
        size_t len = (size_t)(nl - buf);
        char line[LINE_LEN];
        memcpy(line, buf, len);
        line[len] = 0;
        if (len && line[len - 1] == '\r') line[len - 1] = 0;
        memmove(buf, nl + 1, blen - len - 1);
        blen -= len + 1;

        int r = handle_line(c, line, buf, &blen);
        if (r < 0) break;
        if (r > 0) quit = 1;
    }

    /* cleanup: remove from user list and rooms, tell the others */
    pthread_mutex_lock(&gmu);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i] == c) clients[i] = NULL;
    if (c->registered)
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i] && clients[i]->registered)
                send_line(clients[i], "MSG LEAVE %s", c->name);
    pthread_mutex_unlock(&gmu);

    logmsg("DISCONNECT user=%s ip=%s (%s)", c->registered ? c->name : "-",
           c->ip, quit ? "QUIT" : "connection lost");
    close(c->fd);
    pthread_mutex_destroy(&c->wmu);
    free(c);
    return NULL;
}

/* ------------------------------------------------------------- main */
int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    logfp = fopen(LOGFILE, "a");
    if (!logfp) { perror("log file"); return 1; }
    mkdir_p(STORAGE_DIR);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(lfd, 16) < 0) { perror("listen"); return 1; }

    logmsg("SERVER_START port=%d nid=%s log=%s storage=%s", PORT, NID, LOGFILE, STORAGE_DIR);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof ca;
        int fd = accept(lfd, (struct sockaddr *)&ca, &cl);
        if (fd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };   /* stuck-peer guard */
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        Client *c = calloc(1, sizeof *c);
        if (!c) { close(fd); continue; }
        c->fd = fd;
        inet_ntop(AF_INET, &ca.sin_addr, c->ip, sizeof c->ip);
        pthread_mutex_init(&c->wmu, NULL);

        int slot = -1;
        pthread_mutex_lock(&gmu);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (!clients[i]) { clients[i] = c; slot = i; break; }
        pthread_mutex_unlock(&gmu);
        if (slot < 0) {
            reply(c, "ERR 011 SERVER_FULL");
            close(fd);
            pthread_mutex_destroy(&c->wmu);
            free(c);
            continue;
        }

        pthread_t t;
        if (pthread_create(&t, NULL, client_thread, c) != 0) {
            pthread_mutex_lock(&gmu);
            clients[slot] = NULL;
            pthread_mutex_unlock(&gmu);
            close(fd);
            pthread_mutex_destroy(&c->wmu);
            free(c);
            continue;
        }
        pthread_detach(t);
    }
}

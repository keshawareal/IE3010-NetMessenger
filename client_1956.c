/*
 * client_1956.c - NetMessenger client (IE3010 Network Programming)
 * Registration number : IT23601956
 * Default server      : 127.0.0.1:7956  (6000 + 1956)
 *
 * Usage: ./client_1956 [host] [port]
 *   Every stdin line is sent to the server as a protocol command.
 *   Local convenience form for files:
 *       SENDFILE <target> <local-path>
 *   (the client computes the size and sends the exact protocol line
 *    "SENDFILE <target> <filename> <filesize>" followed by the raw bytes).
 *   Files forwarded by the server ("MSG FILE <sender> <name> <size>") are
 *   saved to ./downloads/<sender>/<name>.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LAST4 1956
#define PORT  (6000 + LAST4)     /* 7956 */
#define BUFSZ 8192

static int sock;

static int send_all(const char *d, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(sock, d + off, len - off, MSG_NOSIGNAL);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)n;
    }
    return 0;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

/* receive thread: prints server lines, saves forwarded files */
static void *recv_thread(void *arg)
{
    (void)arg;
    char buf[BUFSZ];
    size_t blen = 0;

    for (;;) {
        char *nl = memchr(buf, '\n', blen);
        if (!nl) {
            if (blen == sizeof buf) blen = 0;
            ssize_t n = recv(sock, buf + blen, sizeof buf - blen, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            blen += (size_t)n;
            continue;
        }
        size_t len = (size_t)(nl - buf);
        char line[BUFSZ];
        memcpy(line, buf, len);
        line[len] = 0;
        memmove(buf, nl + 1, blen - len - 1);
        blen -= len + 1;

        char sender[64], fname[256];
        unsigned long long size;
        if (sscanf(line, "MSG FILE %63s %255s %llu", sender, fname, &size) == 3) {
            char dir[400], path[700];
            snprintf(dir, sizeof dir, "./downloads/%s", sender);
            mkdir_p(dir);
            snprintf(path, sizeof path, "%s/%s", dir, fname);
            FILE *out = fopen(path, "wb");
            unsigned long long left = size;
            int broken = 0;
            while (left > 0) {
                char tmp[BUFSZ];
                size_t n;
                if (blen > 0) {
                    n = blen < left ? blen : (size_t)left;
                    memcpy(tmp, buf, n);
                    memmove(buf, buf + n, blen - n);
                    blen -= n;
                } else {
                    size_t want = left < sizeof tmp ? (size_t)left : sizeof tmp;
                    ssize_t r = recv(sock, tmp, want, 0);
                    if (r < 0 && errno == EINTR) continue;
                    if (r <= 0) { broken = 1; break; }
                    n = (size_t)r;
                }
                if (out) fwrite(tmp, 1, n, out);
                left -= n;
            }
            if (out) fclose(out);
            if (broken) break;
            printf("[file received] from=%s name=%s size=%llu saved=%s\n",
                   sender, fname, size, path);
            fflush(stdout);
            continue;
        }
        printf("%s\n", line);
        fflush(stdout);
    }
    printf("[disconnected from server]\n");
    fflush(stdout);
    exit(0);
    return NULL;
}

static void send_file_command(char *args)
{
    char *sp = NULL;
    char *target = strtok_r(args, " ", &sp);
    char *path   = strtok_r(NULL, " ", &sp);
    if (!target || !path) { fprintf(stderr, "usage: SENDFILE <target> <path>\n"); return; }

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno)); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    char hdr[1024];
    int n = snprintf(hdr, sizeof hdr, "SENDFILE %s %s %ld\n", target, base, size);
    if (send_all(hdr, (size_t)n) < 0) { fclose(f); return; }

    char tmp[BUFSZ];
    size_t r;
    while ((r = fread(tmp, 1, sizeof tmp, f)) > 0)
        if (send_all(tmp, r) < 0) break;
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : PORT;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof srv);
    srv.sin_family = AF_INET;
    srv.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &srv.sin_addr) != 1) {
        fprintf(stderr, "bad address: %s\n", host);
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&srv, sizeof srv) < 0) {
        perror("connect");
        return 1;
    }
    fprintf(stderr, "[connected to %s:%d]\n", host, port);

    pthread_t t;
    pthread_create(&t, NULL, recv_thread, NULL);

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;

        if (!strncasecmp(line, "SENDFILE ", 9)) {
            send_file_command(line + 9);
            continue;
        }
        line[len++] = '\n';
        if (send_all(line, len) < 0) break;
    }
    shutdown(sock, SHUT_WR);        /* stdin finished: let server close us */
    pthread_join(t, NULL);
    return 0;
}

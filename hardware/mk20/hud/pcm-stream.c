/* Single-use loopback PCM bridge for the Snowball lab PoC.
 * ADB forwards a host loopback socket here; no public LAN audio listener.
 * Protocol: 32 hex nonce + LF -> SBP1 + LE32(16000), then LE32 length + PCM.
 * Client sends STOP\n to finish. Terminal frame: LE32(0), LE32(exit status).
 * Each process owns exactly one arecord child; both have bounded lifetimes.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int write_all(int fd, const void *data, size_t count) {
    const char *p = data;
    while (count) {
        ssize_t n = send(fd, p, count, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; count -= (size_t)n;
    }
    return 0;
}
static int number(int fd, uint32_t n) {
    unsigned char b[4] = { n & 255, (n >> 8) & 255, (n >> 16) & 255, n >> 24 };
    return write_all(fd, b, sizeof b);
}
static long long millis(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int main(int argc, char **argv) {
    if ((argc != 3 && argc != 4) || strlen(argv[2]) != 32 || strspn(argv[2], "0123456789abcdef") != 32) return 2;
    int mic = argc == 4 ? atoi(argv[3]) - 1 : 2;
    if (mic < 0 || mic > 2) return 2;
    int port = atoi(argv[1]);
    if (port < 1024 || port > 65535) return 2;
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, sizeof addr) || listen(server, 1)) return 3;
    struct pollfd waiting = { .fd = server, .events = POLLIN };
    if (poll(&waiting, 1, 15000) <= 0) { close(server); return 4; }
    int client = accept(server, NULL, NULL); close(server);
    if (client < 0) return 4;
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    char token[33]; size_t got = 0;
    while (got < sizeof token) {
        ssize_t n = recv(client, token + got, sizeof token - got, 0);
        if (n <= 0) { close(client); return 5; }
        got += (size_t)n;
    }
    if (memcmp(token, argv[2], 32) || token[32] != '\n') { close(client); return 5; }
    int audio[2]; if (pipe(audio)) { close(client); return 6; }
    pid_t child = fork();
    if (child < 0) return 6;
    if (child == 0) {
        prctl(PR_SET_PDEATHSIG, SIGINT);
        if (getppid() == 1) _exit(1);
        close(audio[0]); close(client); dup2(audio[1], STDOUT_FILENO); close(audio[1]);
        execlp("arecord", "arecord", "-q", "-D", "hw:0,0", "-f", "S16_LE", "-r", "16000",
               "-c", "3", "-t", "raw", "-d", "60", "-F", "20000", "-B", "80000", (char *)NULL);
        _exit(127);
    }
    close(audio[1]);
    int connected = write_all(client, "SBP1", 4) == 0 && number(client, 16000) == 0;
    int stopping = !connected;
    long long deadline = millis() + 65000;
    if (stopping) { kill(child, SIGINT); deadline = millis() + 1500; }
    /* The board's capture DMA has three mic channels. Request all three and
       select one microphone explicitly rather than trusting mono DMA setup.
       Default MIC3 has the strongest observed signal; validate with speech. */
    unsigned char buffer[12288], mono[4096];
    size_t carry = 0;
    while (millis() < deadline) {
        struct pollfd fds[2] = { { .fd = audio[0], .events = POLLIN },
                                { .fd = stopping ? -1 : client, .events = POLLIN } };
        int r = poll(fds, 2, 100);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) break;
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char command[16]; ssize_t n = recv(client, command, sizeof command, 0);
            // Any control input stops capture; no extra capability can be requested.
            if (n <= 0) connected = 0;
            stopping = 1; kill(child, SIGINT); deadline = millis() + 1500;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(audio[0], buffer + carry, sizeof buffer - carry);
            if (n <= 0) break;
            size_t total = (size_t)n + carry, frames = total / 6;
            for (size_t i = 0; i < frames; i++) { mono[i*2] = buffer[i*6+mic*2]; mono[i*2+1] = buffer[i*6+mic*2+1]; }
            carry = total % 6;
            if (carry) memmove(buffer, buffer + frames*6, carry);
            if (connected && frames && (number(client, (uint32_t)(frames*2)) || write_all(client, mono, frames*2))) {
                connected = 0; stopping = 1; kill(child, SIGINT); deadline = millis() + 1500;
            }
        }
    }
    close(audio[0]);
    int status = 0;
    if (waitpid(child, &status, WNOHANG) == 0) {
        kill(child, SIGINT);
        for (int i = 0; i < 10; i++) {
            struct timespec delay = { .tv_sec = 0, .tv_nsec = 20000000 }; nanosleep(&delay, NULL);
            if (waitpid(child, &status, WNOHANG) == child) goto reaped;
        }
        kill(child, SIGKILL); waitpid(child, &status, 0);
    }
reaped:
    if (connected) {
        uint32_t code = WIFEXITED(status) ? (uint32_t)WEXITSTATUS(status) :
            (stopping && WIFSIGNALED(status) && WTERMSIG(status) == SIGINT ? 0 : 1);
        number(client, 0); number(client, code);
    }
    close(client);
    return 0;
}

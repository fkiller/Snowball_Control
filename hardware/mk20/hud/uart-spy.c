#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

static int (*real_open)(const char *path, int flags, ...) = NULL;
static ssize_t (*real_write)(int fd, const void *buf, size_t count) = NULL;
static ssize_t (*real_read)(int fd, void *buf, size_t count) = NULL;
static int s_tty_fd = -1;
static FILE *s_log = NULL;

int open(const char *path, int flags, ...) {
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    int fd = real_open(path, flags);
    if (path && strstr(path, "ttyS1")) {
        s_tty_fd = fd;
        if (!s_log) s_log = fopen("/tmp/uart_spy.log", "w");
        if (s_log) {
            fprintf(s_log, "[SPY] Opened %s as fd %d\n", path, fd);
            fflush(s_log);
        }
    }
    return fd;
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!real_write) real_write = dlsym(RTLD_NEXT, "write");
    ssize_t ret = real_write(fd, buf, count);
    if (fd == s_tty_fd && s_log && count > 0) {
        fprintf(s_log, "[SPY WRITE %zu] ", count);
        const unsigned char *b = (const unsigned char *)buf;
        for (size_t i = 0; i < count; i++) fprintf(s_log, "%02X ", b[i]);
        fprintf(s_log, "\n");
        fflush(s_log);
    }
    return ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    if (!real_read) real_read = dlsym(RTLD_NEXT, "read");
    ssize_t ret = real_read(fd, buf, count);
    if (fd == s_tty_fd && s_log && ret > 0) {
        fprintf(s_log, "[SPY READ %zd] ", ret);
        const unsigned char *b = (const unsigned char *)buf;
        for (ssize_t i = 0; i < ret; i++) fprintf(s_log, "%02X ", b[i]);
        fprintf(s_log, "\n");
        fflush(s_log);
    }
    return ret;
}

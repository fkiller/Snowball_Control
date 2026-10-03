/*
 * input-sniff.c - Hardware input sniffer for MK20
 * Monitors /dev/input/event0, event1, event2, /dev/ttyS1, /dev/ttyGS0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <linux/input.h>

static int open_serial(const char *path, int baud) {
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("[-] Failed to open %s\n", path);
        return -1;
    }
    struct termios tty;
    if (tcgetattr(fd, &tty) == 0) {
        cfmakeraw(&tty);
        cfsetispeed(&tty, baud);
        cfsetospeed(&tty, baud);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
        tcsetattr(fd, TCSANOW, &tty);
        tcflush(fd, TCIFLUSH);
    }
    printf("[+] Opened %s (fd %d)\n", path, fd);
    return fd;
}

static int open_input(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("[-] Failed to open %s\n", path);
        return -1;
    }
    char name[256] = "Unknown";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    printf("[+] Opened %s: '%s' (fd %d)\n", path, name, fd);
    return fd;
}

int main(int argc, char *argv[]) {
    printf("=== MK20 Input Hardware Sniffer ===\n");

    int fd_ev0 = open_input("/dev/input/event0");
    int fd_ev1 = open_input("/dev/input/event1");
    int fd_ev2 = open_input("/dev/input/event2");
    int fd_s1  = open_serial("/dev/ttyS1", B115200);
    int fd_gs0 = open_serial("/dev/ttyGS0", B115200);

    struct pollfd fds[5];
    int count = 0;
    int map[5];

    if (fd_ev0 >= 0) { fds[count].fd = fd_ev0; fds[count].events = POLLIN; map[count++] = 0; }
    if (fd_ev1 >= 0) { fds[count].fd = fd_ev1; fds[count].events = POLLIN; map[count++] = 1; }
    if (fd_ev2 >= 0) { fds[count].fd = fd_ev2; fds[count].events = POLLIN; map[count++] = 2; }
    if (fd_s1 >= 0)  { fds[count].fd = fd_s1;  fds[count].events = POLLIN; map[count++] = 3; }
    if (fd_gs0 >= 0) { fds[count].fd = fd_gs0; fds[count].events = POLLIN; map[count++] = 4; }

    printf("[*] Listening on %d input devices. Press buttons or turn dial now...\n", count);
    fflush(stdout);

    uint8_t buf[256];
    while (1) {
        int ret = poll(fds, count, 1000);
        if (ret > 0) {
            for (int i = 0; i < count; i++) {
                if (fds[i].revents & POLLIN) {
                    int devType = map[i];
                    int n = read(fds[i].fd, buf, sizeof(buf));
                    if (n > 0) {
                        if (devType <= 2) {
                            // Linux Input Event
                            struct input_event *ev = (struct input_event *)buf;
                            int num_ev = n / sizeof(struct input_event);
                            for (int e = 0; e < num_ev; e++) {
                                printf("[INPUT-EVENT%d] type=%u code=%u val=%d\n", 
                                       devType, ev[e].type, ev[e].code, ev[e].value);
                            }
                        } else {
                            // Serial UART
                            const char *name = (devType == 3) ? "ttyS1" : "ttyGS0";
                            printf("[%s] Got %d bytes: ", name, n);
                            for (int b = 0; b < n; b++) {
                                printf("%02X ", buf[b]);
                            }
                            printf("\n");
                        }
                        fflush(stdout);
                    }
                }
            }
        }
    }
    return 0;
}

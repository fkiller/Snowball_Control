#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>

int main() {
    int fd = open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) { perror("open /dev/ttyS1"); return 1; }

    struct termios opt;
    tcgetattr(fd, &opt);
    cfsetispeed(&opt, B115200);
    cfsetospeed(&opt, B115200);
    opt.c_cflag |= (CLOCAL | CREAD | CS8);
    opt.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS | CSIZE);
    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY);
    opt.c_oflag &= ~OPOST;
    opt.c_cc[VMIN] = 0;
    opt.c_cc[VTIME] = 10; // 1 sec
    tcsetattr(fd, TCSANOW, &opt);
    tcflush(fd, TCIOFLUSH);

    // Frame: AA 55 sum len ~len data... F5 5F
    // id_get_protocol_version: data = [0x01]
    uint8_t frame[] = { 0xAA, 0x55, 0x01, 0x01, 0xFE, 0x01, 0xF5, 0x5F };
    printf("Sending get_protocol_version (%zu bytes)...\n", sizeof(frame));
    write(fd, frame, sizeof(frame));

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int r = poll(&pfd, 1, 2000);
    if (r > 0 && (pfd.revents & POLLIN)) {
        uint8_t buf[256];
        int n = read(fd, buf, sizeof(buf));
        printf("Received %d bytes: ", n);
        for (int i = 0; i < n; i++) printf("%02X ", buf[i]);
        printf("\n");
    } else {
        printf("Timeout waiting for GD32 reply on /dev/ttyS1 (poll ret=%d, revents=0x%X)\n", r, pfd.revents);
    }
    close(fd);
    return 0;
}

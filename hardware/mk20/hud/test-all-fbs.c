#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    printf("=== Testing Physical Key Screens /dev/fb1 .. /dev/fb20 and /dev/fb21 ===\n");
    
    // Fill each key with an accent color
    for (int k = 1; k <= 20; k++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/fb%d", k);
        int fd = open(path, O_RDWR);
        if (fd >= 0) {
            uint16_t *buf = (uint16_t *)mmap(0, 128 * 128 * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (buf != MAP_FAILED) {
                // Color gradient by key
                uint16_t color;
                if (k <= 4) color = 0x07E0;      // Green (Action Strip)
                else if (k <= 8) color = 0xF800; // Red
                else if (k <= 12) color = 0x001F; // Blue
                else if (k <= 16) color = 0xFFE0; // Yellow
                else color = 0xF81F;              // Magenta
                
                for (int i = 0; i < 128 * 128; i++) {
                    buf[i] = color;
                }
                munmap(buf, 128 * 128 * 2);
            }
            close(fd);
            printf("Key %d (%s) colored successfully\n", k, path);
        }
    }
    
    // Top display /dev/fb21 (428x142)
    int fd21 = open("/dev/fb21", O_RDWR);
    if (fd21 >= 0) {
        uint16_t *buf = (uint16_t *)mmap(0, 428 * 142 * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd21, 0);
        if (buf != MAP_FAILED) {
            for (int i = 0; i < 428 * 142; i++) {
                buf[i] = 0x18E3; // Dark Slate / Cyan
            }
            munmap(buf, 428 * 142 * 2);
        }
        close(fd21);
        printf("Top display (/dev/fb21) colored successfully\n");
    }
    return 0;
}

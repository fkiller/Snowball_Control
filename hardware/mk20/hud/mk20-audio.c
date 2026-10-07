/**
 * mk20-audio: Native Audio Daemon for Snowball MK20 Terminal
 * 
 * Replaces legacy ADB-based audio capture and playback with a native,
 * low-latency TCP audio server running directly on MK20 Tina Linux.
 * 
 * Features:
 * - TCP Server on port 7702 (INADDR_ANY)
 * - SNAU binary protocol:
 *     - Header (16 bytes):
 *         magic (4 bytes): 'S','N','A','U'
 *         mode (1 byte): 1=PLAY (TTS), 2=RECORD (STT), 3=PING
 *         channels (1 byte): 1=Mono, 2=Stereo
 *         volume (1 byte): 0..100 (Host context volume)
 *         muted (1 byte): 0=Normal, 1=Muted
 *         sample_rate (4 bytes): LE32 (e.g. 24000, 16000)
 *         data_len (4 bytes): LE32 (or 0xFFFFFFFF for streaming)
 * - TTS Playback: Streams 16-bit PCM directly into ALSA aplay with software & hardware volume scaling
 * - STT Recording: Streams 16-bit 16kHz PCM directly from ALSA arecord (MIC3 channel) to TCP socket
 * - Zero ADB dependency, zero file I/O, zero disk wear
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#define AUDIO_PORT 7702
#define SNAU_MAGIC 0x55414E53 // 'SNAU' in little endian

#define MODE_PLAY        1
#define MODE_RECORD      2
#define MODE_PING        3
#define MODE_SET_VOLUME  4
#define MODE_STOP 5

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  mode;
    uint8_t  channels;
    uint8_t  volume;
    uint8_t  muted;
    uint32_t sample_rate;
    uint32_t data_len;
} SnauHeader;
#pragma pack(pop)

static volatile sig_atomic_t g_running = 1;
static char g_owner_ip[INET_ADDRSTRLEN],g_lease[33];
static int selected_owner(void) {
    FILE *f=fopen("/tmp/snowball-audio.owner","r"); char ip[INET_ADDRSTRLEN],lease[33];
    if(!f)return 0;
    long long expires=0;int got=fscanf(f,"%15s %32s %lld",ip,lease,&expires);fclose(f);
    return got==3&&expires>=time(NULL)&&!strcmp(ip,g_owner_ip)&&!strcmp(lease,g_lease);
}
static int finish_child(pid_t pid,int allow_drain) {
    int status=0;
    for(int i=0;i<(allow_drain?1500:20);i++){
        if(waitpid(pid,&status,WNOHANG)==pid)return WIFEXITED(status)&&WEXITSTATUS(status)==0;
        if(!g_running||!selected_owner())break;
        usleep(100000);
    }
    kill(pid,SIGTERM);for(int i=0;i<10;i++){if(waitpid(pid,&status,WNOHANG)==pid)return 0;usleep(20000);}
    kill(pid,SIGKILL);waitpid(pid,&status,0);return 0;
}

static void handle_sigterm(int sig) {
    (void)sig;
    g_running = 0;
}

static ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < count) {
        ssize_t n = recv(fd, p + total, count - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return (ssize_t)total; // EOF
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < count) {
        ssize_t n = send(fd, p + total, count - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static void apply_hardware_volume(uint8_t volume, uint8_t muted) {
    // MK20 Tina Linux mixer controls:
    // LINEOUT volume (0-31), Headphone volume (0-7), DAC volume (0-255)
    uint8_t eff_vol = muted ? 0 : (volume > 100 ? 100 : volume);
    int lineout_val = (eff_vol * 31) / 100;
    int hp_val = (eff_vol * 7) / 100;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "amixer -q sset 'LINEOUT volume' %d 2>/dev/null", lineout_val);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "amixer -q sset 'Headphone volume' %d 2>/dev/null", hp_val);
    system(cmd);
}

static void handle_playback(int client_fd, const SnauHeader *hdr) {
    printf("[mk20-audio] Playback started: %u Hz, %u ch, vol=%u%%, muted=%u\n",
           hdr->sample_rate, hdr->channels, hdr->volume, hdr->muted);

    apply_hardware_volume(hdr->volume, hdr->muted);

    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }

    if (pid == 0) {
        // Child: run aplay
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        close(pipe_fd[1]);
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);

        char rate_str[16], ch_str[8];
        snprintf(rate_str, sizeof(rate_str), "%u", hdr->sample_rate ? hdr->sample_rate : 24000);
        snprintf(ch_str, sizeof(ch_str), "%u", hdr->channels ? hdr->channels : 1);

        execlp("aplay", "aplay", "-q",
               "-D", "default",
               "-f", "S16_LE",
               "-r", rate_str,
               "-c", ch_str,
               "-t", "raw",
               (char *)NULL);
        _exit(127);
    }

    // Parent: read from socket, scale volume in software, and stream into pipe
    close(pipe_fd[0]);

    /* Live mixer gain is controlled by the accepting parent, avoiding double attenuation. */
    uint8_t eff_vol = 100;
    if(write_all(client_fd,"RDY1",4)<0){close(pipe_fd[1]);kill(pid,SIGTERM);finish_child(pid,0);return;}
    int16_t buffer[2048];
    size_t remaining = (hdr->data_len != 0xFFFFFFFF) ? hdr->data_len : (size_t)-1;

    time_t last_data=time(NULL),started=last_data;
    while (g_running && selected_owner() && time(NULL)-last_data<5 && time(NULL)-started<120) {
        struct pollfd ready={.fd=client_fd,.events=POLLIN};
        int available=poll(&ready,1,200);
        if(available<0&&errno!=EINTR)break;
        if(available<=0)continue;
        size_t to_read = sizeof(buffer);
        if (remaining != (size_t)-1 && to_read > remaining) {
            to_read = remaining;
        }
        if (to_read == 0) break;

        ssize_t n = recv(client_fd, buffer, to_read, 0);
        if (n <= 0) break; // Client finished or disconnected
        last_data=time(NULL);

        // Apply software volume scaling
        if (eff_vol == 0) {
            memset(buffer, 0, (size_t)n);
        } else if (eff_vol < 100) {
            size_t samples = (size_t)n / sizeof(int16_t);
            for (size_t i = 0; i < samples; i++) {
                buffer[i] = (int16_t)(((int32_t)buffer[i] * eff_vol) / 100);
            }
        }

        // Write to aplay pipe
        ssize_t written = 0;
        char *ptr = (char *)buffer;
        while (written < n) {
            ssize_t w = write(pipe_fd[1], ptr + written, (size_t)(n - written));
            if (w <= 0) {
                if (errno == EINTR) continue;
                goto play_done;
            }
            written += w;
        }

        if (remaining != (size_t)-1) {
            remaining -= (size_t)n;
        }
    }

play_done:
    close(pipe_fd[1]);
    int ok=finish_child(pid,remaining==0);
    write_all(client_fd,ok&&remaining==0?"DONE":"ERR1",4);
    printf("[mk20-audio] Playback finished\n");
}

static void handle_record(int client_fd, const SnauHeader *hdr) {
    printf("[mk20-audio] Recording started (16kHz 16-bit Mono, MIC3)\n");
    (void)hdr;

    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        perror("pipe");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }

    if (pid == 0) {
        // Child: run arecord on hardware 3-channel input
        prctl(PR_SET_PDEATHSIG, SIGINT);
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        execlp("arecord", "arecord", "-q",
               "-D", "hw:0,0",
               "-f", "S16_LE",
               "-r", "16000",
               "-c", "3",
               "-t", "raw",
               "-d", "120",
               (char *)NULL);
        _exit(127);
    }

    // Parent: read 3-channel frames, extract MIC3 (channel 2), and stream to TCP socket
    close(pipe_fd[1]);

    uint8_t in_buf[12288];
    uint8_t mono_buf[4096];
    size_t carry = 0;
    int sent_ready=0;
    time_t started=time(NULL);

    struct pollfd pfd[2];
    pfd[0].fd = pipe_fd[0];
    pfd[0].events = POLLIN;
    pfd[1].fd = client_fd;
    pfd[1].events = POLLIN;

    while (g_running && selected_owner() && time(NULL)-started<120) {
        int pr = poll(pfd, 2, 200);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) continue;

        // If client sends data or closes socket, abort recording
        if (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char tmp[16];
            recv(client_fd, tmp, sizeof(tmp), 0);
            break;
        }

        // Microphone data available
        if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(pipe_fd[0], in_buf + carry, sizeof(in_buf) - carry);
            if (n <= 0) break;

            size_t total = (size_t)n + carry;
            size_t frames = total / 6; // 3 channels * 2 bytes = 6 bytes/frame
            for (size_t i = 0; i < frames; i++) {
                // Channel 3 is at offset 4 and 5
                mono_buf[i * 2]     = in_buf[i * 6 + 4];
                mono_buf[i * 2 + 1] = in_buf[i * 6 + 5];
            }
            carry = total % 6;
            if (carry) {
                memmove(in_buf, in_buf + frames * 6, carry);
            }

            if (frames > 0) {
                if(!sent_ready){if(write_all(client_fd,"RDY1",4)<0)break;sent_ready=1;}
                if (write_all(client_fd, mono_buf, frames * 2) < 0) {
                    break; // Client closed connection
                }
            }
        }
    }

    close(pipe_fd[0]);
    kill(pid, SIGINT);
    finish_child(pid,0);
    printf("[mk20-audio] Recording finished\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            if (daemon(1, 1) < 0) {
                perror("daemon");
            }
            break;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigterm);
    signal(SIGTERM, handle_sigterm);

    printf("=========================================\n");
    printf("  MK20 Native Audio Daemon (mk20-audiod)\n");
    printf("  Listening on TCP port %d\n", AUDIO_PORT);
    printf("=========================================\n");

    // Initialize ALSA mixer default switches
    system("amixer -q sset 'HpSpeaker' on 2>/dev/null");
    system("amixer -q sset 'LINEOUT' on 2>/dev/null");
    system("amixer -q sset 'Headphone' on 2>/dev/null");
    system("amixer -q sset 'MIC3 Input Select' on 2>/dev/null");
    system("amixer -q sset 'MIC3 gain volume' 28 2>/dev/null");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(AUDIO_PORT);

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 4) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    pid_t worker=0;
    while(g_running){
        if(worker&&waitpid(worker,NULL,WNOHANG)==worker)worker=0;
        struct pollfd incoming={.fd=server_fd,.events=POLLIN};
        if(poll(&incoming,1,100)<=0)continue;
        struct sockaddr_in peer;socklen_t len=sizeof peer;
        int fd=accept(server_fd,(struct sockaddr *)&peer,&len);if(fd<0)continue;
        struct timeval timeout={.tv_sec=0,.tv_usec=300000};
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
        timeout.tv_sec=2;timeout.tv_usec=0;setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
        SnauHeader hdr;
        if(read_exact(fd,&hdr,sizeof hdr)!=sizeof hdr||hdr.magic!=SNAU_MAGIC){close(fd);continue;}
        if(hdr.mode==MODE_PING){write_all(fd,"PONG",4);close(fd);continue;}
        int versioned=hdr.mode&0x80;hdr.mode&=0x7f;
        inet_ntop(AF_INET,&peer.sin_addr,g_owner_ip,sizeof g_owner_ip);
        memset(g_lease,0,sizeof g_lease);
        if(!versioned||read_exact(fd,g_lease,32)!=32||!selected_owner()){
            write_all(fd,"ERR1",4);close(fd);continue;
        }
        if(hdr.mode==MODE_STOP){
            if(worker){kill(-worker,SIGTERM);for(int i=0;i<25;i++){if(waitpid(worker,NULL,WNOHANG)==worker){worker=0;break;}usleep(20000);}
                if(worker){kill(-worker,SIGKILL);waitpid(worker,NULL,0);worker=0;}}
            write_all(fd,"RDY1",4);close(fd);continue;
        }
        if(hdr.mode==MODE_SET_VOLUME){apply_hardware_volume(hdr.volume,hdr.muted);write_all(fd,"RDY1",4);close(fd);continue;}
        if(worker||(hdr.mode!=MODE_PLAY&&hdr.mode!=MODE_RECORD)||hdr.channels<1||hdr.channels>2||hdr.sample_rate<8000||hdr.sample_rate>48000||
            (hdr.mode==MODE_PLAY&&(hdr.data_len==0||hdr.data_len>16000000||hdr.data_len%(hdr.channels*2)))){
            write_all(fd,"ERR1",4);close(fd);continue;
        }
        worker=fork();
        if(worker==0){
            setpgid(0,0);close(server_fd);
            struct timeval stream_timeout={.tv_sec=5,.tv_usec=0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&stream_timeout,sizeof stream_timeout);
            if(hdr.mode==MODE_PLAY)handle_playback(fd,&hdr);else handle_record(fd,&hdr);
            close(fd);_exit(0);
        }
        if(worker<0){worker=0;write_all(fd,"ERR1",4);}else setpgid(worker,worker);
        close(fd);
    }
    if(worker){kill(-worker,SIGTERM);usleep(300000);kill(-worker,SIGKILL);waitpid(worker,NULL,0);}

    close(server_fd);
    printf("[mk20-audio] Daemon terminated cleanly.\n");
    return 0;
}

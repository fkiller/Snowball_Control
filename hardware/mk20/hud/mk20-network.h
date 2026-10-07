#ifndef MK20_NETWORK_H
#define MK20_NETWORK_H
#include <netinet/in.h>
#include <stdint.h>
#define MK20_HOST_LIMIT 16
typedef struct { char id[65], name[64]; struct sockaddr_in peer; uint64_t seen; int paired; } Mk20Host;
typedef struct {
    Mk20Host hosts[MK20_HOST_LIMIT]; int count, selected, focus, menu;
    char device_id[18], selected_id[65], lease[33]; uint64_t last_tick;
} Mk20Network;
int mk20_network_init(Mk20Network *n);
int mk20_network_packet(Mk20Network *n, const char *packet, const struct sockaddr_in *peer, uint64_t now);
void mk20_network_tick(Mk20Network *n, int socket, uint64_t now);
int mk20_network_select(Mk20Network *n, int socket, int index, uint64_t now);
void mk20_network_forget(Mk20Network *n, int socket, int index);
int mk20_network_accept(const Mk20Network *n, const char *packet, const struct sockaddr_in *peer);
#endif

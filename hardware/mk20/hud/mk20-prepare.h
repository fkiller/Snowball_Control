#ifndef MK20_PREPARE_H
#define MK20_PREPARE_H
#include "mk20-network.h"
int mk20_prepare_probe(const Mk20Network *network, int socket, const char *packet, const struct sockaddr_in *peer);
#endif

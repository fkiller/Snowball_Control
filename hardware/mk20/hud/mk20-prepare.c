#define _DEFAULT_SOURCE
#include "mk20-prepare.h"
#include "../firmware/runtime-version.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static int hex(const char *text, size_t length) {
    if(strlen(text)!=length)return 0;
    for(size_t i=0;i<length;i++)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return 0;
    return 1;
}
static int running_digest(long pid,char out[65]) {
    char command[96],link[64],target[256];
    if(pid<=0)return 0;
    snprintf(link,sizeof link,"/proc/%ld/exe",pid);
    ssize_t length=readlink(link,target,sizeof target-1);if(length<=0)return 0;target[length]=0;
    if(strcmp(target,"/mnt/SDCARD/mk20-hud")&&strcmp(target,"/mnt/SDCARD/mk20-audio"))return 0;
    snprintf(command,sizeof command,"sha256sum /proc/%ld/exe 2>/dev/null",pid);
    FILE *stream=popen(command,"r");if(!stream)return 0;
    int ok=fscanf(stream,"%64s",out)==1;pclose(stream);return ok&&hex(out,64);
}
int mk20_prepare_probe(const Mk20Network *network,int socket,const char *packet,const struct sockaddr_in *peer) {
    if(strncmp(packet,"SNMP1\tPROBE\t",12))return 0;
    unsigned long ip=ntohl(peer->sin_addr.s_addr);
    if(!((ip>>24)==10||(ip>>16)==0xc0a8||(ip>>20)==0xac1)||ntohs(peer->sin_port)<1024||!network->device_id[0])return 1;
    const char *nonce=packet+12;if(!hex(nonce,32))return 1;
    static time_t last;static unsigned count;time_t now=time(NULL);if(now!=last){last=now;count=0;}if(++count>4)return 1;
    char receipt[64],hud[65],audio[65],reply[320];
    FILE *stream=fopen("/mnt/SDCARD/snowball-setup.receipt","r");if(!stream)return 1;
    int ok=fgets(receipt,sizeof receipt,stream)!=NULL;fclose(stream);if(!ok)return 1;
    receipt[strcspn(receipt,"\r\n")]=0;if(!hex(receipt,32)||!running_digest((long)getpid(),hud))return 1;
    stream=popen("pidof mk20-audio","r");if(!stream)return 1;long pid=0;char extra=0;
    ok=fscanf(stream,"%ld %c",&pid,&extra)==1;pclose(stream);if(!ok||!running_digest(pid,audio))return 1;
    int len=snprintf(reply,sizeof reply,"SNMP1\tINFO\t%s\t%s\t%s\t%s\t%s\t%s",nonce,receipt,SNOWBALL_RUNTIME_VERSION,network->device_id,hud,audio);
    if(len>0&&(size_t)len<sizeof reply)sendto(socket,reply,len,0,(const struct sockaddr *)peer,sizeof *peer);
    return 1;
}

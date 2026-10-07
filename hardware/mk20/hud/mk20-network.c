#define _DEFAULT_SOURCE
#include "mk20-network.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <time.h>
#ifndef REGISTRY
#define REGISTRY "/mnt/SDCARD/snowball-hosts.v1"
#endif
#ifndef OWNER
#define OWNER "/tmp/snowball-audio.owner"
#endif
#define DISCOVERY_PORT 47772
static int identifier(const char *s) {
    size_t len=strlen(s); if(!len||len>64)return 0;
    for(size_t i=0;i<len;i++)if(!isalnum((unsigned char)s[i])&&s[i]!='_'&&s[i]!='-')return 0;
    return 1;
}
static int private_ip(uint32_t ip) { ip=ntohl(ip); return (ip>>24)==10 || (ip>>16)==0xc0a8 || (ip>>20)==0xac1; }
static int save(Mk20Network *n) {
    FILE *f=fopen(REGISTRY ".tmp","w"); if(!f)return 0;
    chmod(REGISTRY ".tmp",0600);
    fprintf(f,"%s\n",n->selected_id);
    for(int i=0;i<n->count;i++)if(n->hosts[i].paired)fprintf(f,"%s\t%s\n",n->hosts[i].id,n->hosts[i].name);
    int ok=!fflush(f)&&!fsync(fileno(f)); if(fclose(f))ok=0;
    return ok&&rename(REGISTRY ".tmp",REGISTRY)==0;
}
static void revoke_owner(void) { unlink(OWNER); }
static int write_owner(Mk20Network *n) {
    FILE *f=fopen(OWNER ".tmp","w"); if(!f)return 0;
    chmod(OWNER ".tmp",0600);
    fprintf(f,"%s %s %lld\n",inet_ntoa(n->hosts[n->selected].peer.sin_addr),n->lease,(long long)time(NULL)+6);
    int ok=!fclose(f); return ok&&rename(OWNER ".tmp",OWNER)==0;
}
static int authorize(Mk20Network *n) {
    unsigned char bytes[16]; int fd=open("/dev/urandom",O_RDONLY);
    if(fd<0)return 0;
    int got=read(fd,bytes,sizeof bytes); close(fd); if(got!=sizeof bytes)return 0;
    for(int i=0;i<16;i++)snprintf(n->lease+i*2,3,"%02x",bytes[i]);
    return write_owner(n);
}
static void send_selection(Mk20Network *n,int socket,const char *kind) {
    if(n->selected<0||!n->lease[0])return;
    Mk20Host *h=&n->hosts[n->selected]; char packet[180];
    int len=snprintf(packet,sizeof packet,"SNMK1\t%s\t%s\t%s\t%s",kind,n->device_id,h->id,n->lease);
    sendto(socket,packet,len,0,(struct sockaddr *)&h->peer,sizeof h->peer);
}
int mk20_network_init(Mk20Network *n) {
    memset(n,0,sizeof *n); n->selected=-1; revoke_owner();
    FILE *f=fopen("/sys/class/net/wlan0/address","r"); char mac[32]={0};
    if(!f)return 0;
    if(!fgets(mac,sizeof mac,f)){fclose(f);return 0;} fclose(f);
    unsigned int b[6]; if(sscanf(mac,"%2x:%2x:%2x:%2x:%2x:%2x",b,b+1,b+2,b+3,b+4,b+5)!=6)return 0;
    snprintf(n->device_id,sizeof n->device_id,"mk20-%02x%02x%02x%02x%02x%02x",b[0],b[1],b[2],b[3],b[4],b[5]);
    f=fopen(REGISTRY,"r"); if(!f)return 1;
    char line[160]; if(fgets(line,sizeof line,f)){line[strcspn(line,"\r\n")]=0;if(identifier(line))strcpy(n->selected_id,line);}
    while(n->count<MK20_HOST_LIMIT&&fgets(line,sizeof line,f)){
        line[strcspn(line,"\r\n")]=0; char *name=strchr(line,'\t');if(!name)continue;*name++=0;
        if(!identifier(line)||strlen(name)>63)continue;
        Mk20Host *h=&n->hosts[n->count++];strcpy(h->id,line);strcpy(h->name,name);h->paired=1;
    }
    fclose(f); return 1;
}
int mk20_network_packet(Mk20Network *n,const char *packet,const struct sockaddr_in *peer,uint64_t now) {
    if(strncmp(packet,"SNMK1\t",6))return 0;
    if(!private_ip(peer->sin_addr.s_addr)||ntohs(peer->sin_port)<1024)return 1;
    char id[65],name[64],tail; if(sscanf(packet,"SNMK1\tOFFER\t%64[^\t]\t%63[^\t]\t1%c",id,name,&tail)!=2||!identifier(id))return 1;
    for(size_t i=0;name[i];i++)if((unsigned char)name[i]<32||(unsigned char)name[i]==127)name[i]='?';
    int index;for(index=0;index<n->count;index++)if(!strcmp(n->hosts[index].id,id))break;
    if(index==n->count){if(n->count==MK20_HOST_LIMIT)return 1;n->count++;strcpy(n->hosts[index].id,id);}
    Mk20Host *h=&n->hosts[index];
    if(n->selected==index&&h->peer.sin_addr.s_addr!=peer->sin_addr.s_addr){revoke_owner();n->selected=-1;n->lease[0]=0;}
    h->peer=*peer;h->seen=now;strcpy(h->name,name); return 1;
}
int mk20_network_select(Mk20Network *n,int socket,int index,uint64_t now) {
    if(index<0||index>=n->count||!n->hosts[index].seen||now-n->hosts[index].seen>8000)return 0;
    send_selection(n,socket,"RELEASE");revoke_owner();n->selected=index;n->lease[0]=0;
    if(!authorize(n)){n->selected=-1;return 0;}
    int was_paired=n->hosts[index].paired;
    n->hosts[index].paired=1;strcpy(n->selected_id,n->hosts[index].id);
    if(!save(n)){n->hosts[index].paired=was_paired;revoke_owner();n->selected=-1;n->lease[0]=0;n->selected_id[0]=0;return 0;}
    send_selection(n,socket,"SELECT");return 1;
}
void mk20_network_forget(Mk20Network *n,int socket,int index) {
    if(index<0||index>=n->count)return;
    if(index==n->selected){send_selection(n,socket,"RELEASE");revoke_owner();n->selected=-1;n->lease[0]=0;n->selected_id[0]=0;}
    if(!strcmp(n->selected_id,n->hosts[index].id))n->selected_id[0]=0;
    n->hosts[index].paired=0;save(n);
}
void mk20_network_tick(Mk20Network *n,int socket,uint64_t now) {
    if(!n->device_id[0]||socket<0||now-n->last_tick<2000)return;
    n->last_tick=now;
    for(int i=0;i<n->count;){
        if(!n->hosts[i].paired&&n->hosts[i].seen&&now-n->hosts[i].seen>30000){
            memmove(n->hosts+i,n->hosts+i+1,(n->count-i-1)*sizeof n->hosts[0]);n->count--;
            if(n->selected>i)n->selected--;
            if(n->focus>=n->count)n->focus=n->count?n->count-1:0;
        }else i++;
    }
    char packet[64];int len=snprintf(packet,sizeof packet,"SNMK1\tDISCOVER\t%s",n->device_id);
    int yes=1;setsockopt(socket,SOL_SOCKET,SO_BROADCAST,&yes,sizeof yes);
    struct ifaddrs *interfaces;if(getifaddrs(&interfaces)==0){
        for(struct ifaddrs *i=interfaces;i;i=i->ifa_next)if(i->ifa_addr&&i->ifa_broadaddr&&i->ifa_addr->sa_family==AF_INET&&(i->ifa_flags&IFF_UP)&&(i->ifa_flags&IFF_BROADCAST)){
            struct sockaddr_in target=*(struct sockaddr_in *)i->ifa_broadaddr;target.sin_port=htons(DISCOVERY_PORT);
            if(private_ip(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr))sendto(socket,packet,len,0,(struct sockaddr *)&target,sizeof target);
        }freeifaddrs(interfaces);
    }
    if(n->selected<0&&n->selected_id[0])for(int i=0;i<n->count;i++)if(n->hosts[i].paired&&!strcmp(n->hosts[i].id,n->selected_id)&&n->hosts[i].seen&&now-n->hosts[i].seen<8000){mk20_network_select(n,socket,i,now);break;}
    if(n->selected>=0){
        if(now-n->hosts[n->selected].seen>8000){revoke_owner();n->selected=-1;n->lease[0]=0;}
        else if(write_owner(n))send_selection(n,socket,"SELECT");
        else {revoke_owner();n->selected=-1;n->lease[0]=0;}
    }
}
int mk20_network_accept(const Mk20Network *n,const char *packet,const struct sockaddr_in *peer) {
    if(n->selected<0||!n->lease[0]||peer->sin_addr.s_addr!=n->hosts[n->selected].peer.sin_addr.s_addr)return 0;
    char scope[48];snprintf(scope,sizeof scope,"\"lease\":\"%s\"",n->lease);return strstr(packet,scope)!=NULL;
}

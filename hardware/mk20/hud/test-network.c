// Native contract test; no framebuffer, hardware input or synthetic runtime.
#include "mk20-network.h"
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    Mk20Network n={.selected=-1};
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(47772)},b=a;
    inet_pton(AF_INET,"192.168.1.10",&a.sin_addr);inet_pton(AF_INET,"192.168.1.11",&b.sin_addr);
    assert(!mk20_network_packet(&n,"{\"type\":\"v2_sync\"}",&a,1000));
    assert(mk20_network_packet(&n,"SNMK1\tOFFER\thost_a\tPC A\t1",&a,1000));
    assert(mk20_network_packet(&n,"SNMK1\tOFFER\thost_b\tPC B\t1",&b,1000));
    assert(n.count==2&&n.hosts[0].peer.sin_addr.s_addr==a.sin_addr.s_addr);
    assert(!n.hosts[0].paired&&n.selected==-1); // discovery cannot enroll
    struct sockaddr_in public=a;inet_pton(AF_INET,"8.8.8.8",&public.sin_addr);
    mk20_network_packet(&n,"SNMK1\tOFFER\thost_c\tPC C\t1",&public,2000);assert(n.count==2);
    mk20_network_packet(&n,"SNMK1\tOFFER\thost_c\tPC C\t1\ttrailing",&a,2000);assert(n.count==2);
    n.selected=0;strcpy(n.lease,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const char *sync="{\"lease\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}";
    assert(mk20_network_accept(&n,sync,&a));assert(!mk20_network_accept(&n,sync,&b));
    assert(!mk20_network_accept(&n,"{}",&a));
    strcpy(n.lease,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");assert(!mk20_network_accept(&n,sync,&a));
    // DHCP updates an existing machine, without adding another or retaining authority.
    mk20_network_packet(&n,"SNMK1\tOFFER\thost_a\tPC A\t1",&b,3000);assert(n.count==2&&n.selected==-1);
    for(int i=0;i<32;i++){char packet[100];snprintf(packet,sizeof packet,"SNMK1\tOFFER\thost_%d\tPC\t1",i);mk20_network_packet(&n,packet,&a,3000);}
    assert(n.count==MK20_HOST_LIMIT);
    int udp=socket(AF_INET,SOCK_DGRAM,0);assert(udp>=0);strcpy(n.device_id,"mk20-aabbccddeeff");
    assert(mk20_network_select(&n,udp,0,4000));assert(n.hosts[0].paired&&n.selected==0&&n.lease[0]);
    mk20_network_tick(&n,udp,40000);assert(n.count==1&&n.hosts[0].paired&&n.selected==-1);
    mk20_network_forget(&n,udp,0);assert(!n.hosts[0].paired&&!n.selected_id[0]);close(udp);
    puts("MK20 native discovery/scope tests passed");return 0;
}

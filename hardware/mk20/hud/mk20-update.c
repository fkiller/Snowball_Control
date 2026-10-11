/* SNMU1: physical USB only, fixed files/operations, no shell or path RPC.
 * This executable is copied to /tmp before use. A partition readback must be
 * acknowledged before writes; an unfinished multi-file commit rolls back at boot.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <linux/fs.h>
#include "../firmware/runtime-version.h"
#ifndef SNMU_ROOT
#define SNMU_ROOT "/mnt/SDCARD"
#endif
#define STAGE SNMU_ROOT "/.snowball-update"
#define COUNT 5
static const char *names[COUNT]={"mk20-hud","mk20-audio","lunch.sh","fonts/D2Coding.ttf","mk20-update"};
static int channel=-1, frozen, transaction, applied;
static unsigned received, kept;
static char backup_hash[65], version[32], nonce[33];
static char hashes[COUNT][65];
static int hex(const char *s,size_t n){if(strlen(s)!=n)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static int transfer(void *data,size_t size,int writing){
    char *p=data;while(size){struct pollfd fd={channel,writing?POLLOUT:POLLIN,0};
        if(poll(&fd,1,30000)<=0||fd.revents&(POLLERR|POLLNVAL))return 0;
        ssize_t n=writing?write(channel,p,size):read(channel,p,size);if(n<=0){if(errno==EINTR)continue;return 0;}p+=n;size-=n;
    }return 1;
}
static int reply(const char *s){return transfer((void*)s,strlen(s),1);}
static int line(char *s,size_t cap){size_t n=0;while(n+1<cap){char c;if(!transfer(&c,1,0))return 0;if(c=='\n'){s[n]=0;return 1;}if(c=='\r'||!c)return 0;s[n++]=c;}return 0;}
static int regular(const char *p){struct stat s;return !lstat(p,&s)&&S_ISREG(s.st_mode);}
static int sha(const char *p,char out[65]){
    /* p is exclusively a compiled path or an internally formatted numeric /proc path. */
    char cmd[512];if(snprintf(cmd,sizeof cmd,"sha256sum '%s' 2>/dev/null",p)>=(int)sizeof cmd)return 0;
    FILE *f=popen(cmd,"r");if(!f)return 0;int ok=fscanf(f,"%64s",out)==1;int status=pclose(f);return ok&&!status&&hex(out,64);
}
static void resume(void){
#ifndef SNMU_TEST
    if(frozen){system("mount -o remount,rw /mnt/SDCARD");system("/bin/sh /mnt/SDCARD/lunch.sh --restart-runtime >/tmp/mk20-update-restart.log 2>&1");}
#endif
    frozen=0;
}
static void stage_path(char out[256],const char *kind,int i){snprintf(out,256,STAGE "/%s%d",kind,i);}
static int cleanup(void){
    char p[256];for(int i=0;i<COUNT;i++){stage_path(p,"new",i);unlink(p);stage_path(p,"old",i);unlink(p);stage_path(p,"copy",i);unlink(p);}
    unlink(STAGE "/journal");unlink(STAGE "/committed");return !rmdir(STAGE)||errno==ENOENT;
}
static int recover(void){
    struct stat directory;if(!lstat(STAGE,&directory)&&!S_ISDIR(directory.st_mode))return 0;
    if(regular(STAGE "/committed"))return cleanup();
    FILE *f=fopen(STAGE "/journal","r");if(!f)return errno==ENOENT;
    unsigned mask=0;int ok=fscanf(f,"%u",&mask)==1;fclose(f);if(!ok||mask>31)return 0;
    char old[256],dest[256];for(int i=0;i<COUNT;i++){
        stage_path(old,"old",i);snprintf(dest,sizeof dest,SNMU_ROOT "/%s",names[i]);
        if(regular(old)){if(rename(old,dest))return 0;}
        else if(!(mask&(1u<<i))&&unlink(dest)&&errno!=ENOENT)return 0;
    }sync();return cleanup();
}
static int wifi_saved(void){
    FILE *f=fopen(SNMU_ROOT "/dev-access.conf","r");if(!f)return 0;char b[4096];int found=0;
    while(fgets(b,sizeof b,f))if(!strncmp(b,"WIFI_SSID=",10)&&strcmp(b+10,"''\n")&&strcmp(b+10,"\"\"\n")&&strcmp(b+10,"\n")&&!strstr(b,"YOUR_WIFI_SSID"))found=1;
    fclose(f);return found;
}
static int running(const char *name,char hash[65]){
#ifdef SNMU_TEST
    char p[256];snprintf(p,sizeof p,SNMU_ROOT "/%s",name);return sha(p,hash);
#else
    DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;int count=0;char wanted[256];snprintf(wanted,sizeof wanted,SNMU_ROOT "/%s",name);
    while((e=readdir(d))){char *end;long pid=strtol(e->d_name,&end,10);if(*end||pid<=0)continue;char p[64],dest[256];snprintf(p,sizeof p,"/proc/%ld/exe",pid);ssize_t n=readlink(p,dest,sizeof dest-1);if(n<=0)continue;dest[n]=0;if(!strcmp(dest,wanted)&&sha(p,hash))count++;}
    closedir(d);return count==1;
#endif
}
static int info(const char *challenge){
    char id[32]="",hud[65]="-",audio[65]="-",out[320];
#ifdef SNMU_TEST
    strcpy(id,"mk20-000000000001");
#else
    FILE *f=fopen("/sys/class/net/wlan0/address","r");if(!f)return 0;char mac[64];if(!fgets(mac,sizeof mac,f)){fclose(f);return 0;}fclose(f);size_t k=5;memcpy(id,"mk20-",5);for(char *p=mac;*p&&k<17;p++)if(*p!=':'&&*p!='\n'&&*p!='\r')id[k++]=*p;id[k]=0;if(k!=17||!hex(id+5,12))return 0;
#endif
    int live=running("mk20-hud",hud)&&running("mk20-audio",audio);
    snprintf(out,sizeof out,"SNMU1 INFO %s %s %s %d %s %s %d\n",challenge,id,SNOWBALL_RUNTIME_VERSION,wifi_saved(),hud,audio,live);
    return reply(out);
}
static int backup(void){
    int raw;uint64_t size;
#ifdef SNMU_TEST
    raw=open(SNMU_ROOT "/partition.bin",O_RDONLY);struct stat st;if(raw<0||fstat(raw,&st))return 0;size=st.st_size;
#else
    /* Refuse a different mount layout instead of reading an unrelated disk. */
    FILE *m=fopen("/proc/mounts","r");char b[1024];int match=0;if(!m)return 0;
    const char *mount_prefix="/dev/mmcblk0p1 /mnt/SDCARD vfat ";
    while(fgets(b,sizeof b,m))if(!strncmp(b,mount_prefix,strlen(mount_prefix)))match++;
    fclose(m);if(match!=1)return 0;
    if(system("/bin/sh /mnt/SDCARD/lunch.sh --stop-runtime >/tmp/mk20-update-stop.log 2>&1"))return 0;
    frozen=1;sync();if(system("mount -o remount,ro /mnt/SDCARD"))return 0;
    raw=open("/dev/mmcblk0p1",O_RDONLY);if(raw<0||ioctl(raw,BLKGETSIZE64,&size)){if(raw>=0)close(raw);return 0;}
#endif
    if(size==0||size>256ull*1024*1024){close(raw);return 0;}
#ifdef SNMU_TEST
    const char *source=SNMU_ROOT "/partition.bin";
#else
    const char *source="/dev/mmcblk0p1";
#endif
    if(!sha(source,backup_hash)){close(raw);return 0;}
    char header[160];snprintf(header,sizeof header,"SNMU1 BACKUP %llu %s\n",(unsigned long long)size,backup_hash);if(!reply(header)){close(raw);return 0;}
    char data[32768];uint64_t left=size;while(left){size_t chunk=left>sizeof data?sizeof data:left;ssize_t n=read(raw,data,chunk);if(n<=0||!transfer(data,n,1)){close(raw);return 0;}left-=n;}
    close(raw);return reply("SNMU1 BACKUP-END\n");
}
static int begin(const char *receipt,const char *v,const char *n){
    if(!hex(receipt,64)||strcmp(receipt,backup_hash)||!hex(n,32)||strlen(v)>20||strspn(v,"0123456789.")!=strlen(v))return 0;
#ifndef SNMU_TEST
    if(system("mount -o remount,rw /mnt/SDCARD"))return 0;
#endif
    struct stat s;if(!lstat(STAGE,&s)||errno!=ENOENT)return 0;
    if(mkdir(STAGE,0700))return 0;
    transaction=1;received=kept=0;applied=0;snprintf(version,sizeof version,"%s",v);strcpy(nonce,n);
    return reply("SNMU1 READY\n");
}
static int keep(int i,const char *expected){
    if(!transaction||i<0||i>=COUNT||(received&(1u<<i))||!hex(expected,64))return 0;
    char p[256],actual[65];snprintf(p,sizeof p,SNMU_ROOT "/%s",names[i]);
    if(!regular(p)||!sha(p,actual)||strcmp(actual,expected))return reply("SNMU1 NEED\n");
    strcpy(hashes[i],actual);received|=1u<<i;kept|=1u<<i;return reply("SNMU1 KEPT\n");
}
static int put(int i,unsigned long size,const char *expected){
    if(!transaction||i<0||i>=COUNT||(received&(1u<<i))||size==0||size>8*1024*1024||!hex(expected,64))return 0;
    struct statvfs fs;if(statvfs(SNMU_ROOT,&fs)||(uint64_t)fs.f_bavail*fs.f_frsize<size+512*1024)return 0;
    char path[256];stage_path(path,"new",i);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0700);if(fd<0)return 0;
    if(!reply("SNMU1 SEND\n")){close(fd);return 0;}char b[32768];unsigned long left=size;int ok=1;
    while(left){size_t n=left>sizeof b?sizeof b:left;if(!transfer(b,n,0)){ok=0;break;}size_t off=0;while(off<n){ssize_t w=write(fd,b+off,n-off);if(w<=0){ok=0;break;}off+=w;}if(!ok)break;left-=n;}
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    char actual[65];if(!ok||!sha(path,actual)||strcmp(actual,expected))return 0;
    strcpy(hashes[i],actual);received|=1u<<i;return reply("SNMU1 STORED\n");
}
/* Hash every preserved settings/registration file without returning its content. */
struct saved {char name[256],hash[65];};
static int settings(struct saved entries[64]){
    DIR *d=opendir(SNMU_ROOT);if(!d)return -1;struct dirent *e;int n=0;
    while((e=readdir(d)))if(!strcmp(e->d_name,"dev-access.conf")||(!strncmp(e->d_name,"snowball-",9)&&strcmp(e->d_name,"snowball-runtime.json")&&strcmp(e->d_name,"snowball-setup.receipt"))){
        char p[512];snprintf(p,sizeof p,SNMU_ROOT "/%s",e->d_name);if(n==64||strchr(e->d_name,'\'')||!regular(p)||!sha(p,entries[n].hash)){closedir(d);return -1;}strcpy(entries[n++].name,e->d_name);
    }closedir(d);return n;
}
static int retain_boot_file(const char *source,const char *old,int index){
    char temp[256];stage_path(temp,"copy",index);
    int in=open(source,O_RDONLY|O_NOFOLLOW),out=open(temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0700);
    if(in<0||out<0){if(in>=0)close(in);if(out>=0)close(out);return 0;}
    char b[32768];ssize_t n;int ok=1;
    while((n=read(in,b,sizeof b))>0){ssize_t offset=0;while(offset<n){ssize_t w=write(out,b+offset,n-offset);if(w<=0){ok=0;break;}offset+=w;}if(!ok)break;}
    if(n<0||fsync(out))ok=0;
    close(in);if(close(out))ok=0;
    if(!ok)return 0;
    if(rename(temp,old))return 0;
    sync();return 1;
}
static int apply(void){
    if(!transaction||received!=31)return 0;
    struct saved before[64],after[64];int count=settings(before);if(count<0)return 0;
    struct stat fonts;if(lstat(SNMU_ROOT "/fonts",&fonts)||!S_ISDIR(fonts.st_mode))return 0;
    unsigned mask=0;for(int i=0;i<COUNT;i++){char p[256];snprintf(p,sizeof p,SNMU_ROOT "/%s",names[i]);struct stat s;if(!lstat(p,&s)){if(!S_ISREG(s.st_mode))return 0;mask|=1u<<i;}else if(errno!=ENOENT)return 0;}
    FILE *j=fopen(STAGE "/journal","wx");if(!j)return 0;fprintf(j,"%u\n",mask);int ok=!fflush(j)&&!fsync(fileno(j));if(fclose(j))ok=0;if(!ok)return 0;sync();
    for(int i=0;i<COUNT;i++){char dest[256],old[256],next[256];snprintf(dest,sizeof dest,SNMU_ROOT "/%s",names[i]);stage_path(old,"old",i);stage_path(next,"new",i);
        if(!(kept&(1u<<i))){
            /* Boot entry and receiver must NEVER have an absent-name window.
             * FAT has no hardlinks; retain a small durable copy, then replace.
             * Other binaries can be recovered by this always-present boot path.
             */
            if(mask&(1u<<i)){
                if(i==2||i==4){if(!retain_boot_file(dest,old,i))return 0;}
                else if(rename(dest,old))return 0;
            }
            if(rename(next,dest))return 0;
            sync();
        }
        char actual[65];if(!sha(dest,actual)||strcmp(actual,hashes[i]))return 0;
    }
    int found=settings(after);if(found!=count)return 0;for(int i=0;i<count;i++){int matches=0;for(int k=0;k<found;k++)if(!strcmp(before[i].name,after[k].name)&&!strcmp(before[i].hash,after[k].hash))matches++;if(matches!=1)return 0;}
    /* Commit marker is durable before obsolete copies can be removed. */
    int fd=open(STAGE "/committed",O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 0;ok=!fsync(fd);if(close(fd))ok=0;sync();if(!ok)return 0;transaction=0;
    if(!cleanup())return 0;
    backup_hash[0]=0;applied=1;return reply("SNMU1 APPLIED\n");
}
static int decode(const char *in,char *out,size_t limit){
    size_t n=strlen(in);if(n%2||n/2>limit||!hex(in,n))return 0;
    for(size_t i=0;i<n/2;i++){char byte[3]={in[i*2],in[i*2+1],0};out[i]=(char)strtoul(byte,NULL,16);if(!out[i]||out[i]=='\n'||out[i]=='\r')return 0;}out[n/2]=0;return 1;
}
static void quoted(FILE *f,const char *s){fputc('\'',f);for(;*s;s++){if(*s=='\'')fputs("'\\''",f);else fputc(*s,f);}fputc('\'',f);}
static int network(const char *ssid_hex,const char *password_hex){
    char ssid[33],password[64];if(!applied||wifi_saved()||!decode(ssid_hex,ssid,32)||!ssid[0])return 0;
    if(!strcmp(password_hex,"-"))password[0]=0;else if(!decode(password_hex,password,63)||strlen(password)<8)return 0;
    const char *temp=SNMU_ROOT "/.snowball-wifi-new";int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return 0;
    FILE *out=fdopen(fd,"w"),*in=fopen(SNMU_ROOT "/dev-access.conf","r");if(!out){close(fd);unlink(temp);return 0;}
    char b[4096];int ok=1;size_t total=0;if(in){while(fgets(b,sizeof b,in)){total+=strlen(b);if(total>65536||(!strchr(b,'\n')&&!feof(in))){ok=0;break;}if(strncmp(b,"WIFI_SSID=",10)&&strncmp(b,"WIFI_PSK=",9))fputs(b,out);}if(ferror(in))ok=0;fclose(in);}
    fputs("\nWIFI_SSID=",out);quoted(out,ssid);fputs("\nWIFI_PSK=",out);quoted(out,password);fputc('\n',out);memset(password,0,sizeof password);
    if(fflush(out)||fsync(fileno(out)))ok=0;
    if(fclose(out))ok=0;
    if(!ok){unlink(temp);return 0;}if(rename(temp,SNMU_ROOT "/dev-access.conf"))return 0;sync();return reply("SNMU1 WIFI-SAVED\n");
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--recover"))return recover()?0:1;
    if(argc!=1)return 1;
#ifdef SNMU_TEST
    channel=0;
#else
    int lock=open("/tmp/mk20-update.lock",O_CREAT|O_WRONLY,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;
    channel=open("/dev/ttyGS0",O_RDWR|O_NOCTTY);if(channel<0)return 1;struct termios t;if(tcgetattr(channel,&t))return 1;cfmakeraw(&t);t.c_cflag|=CLOCAL|CREAD;if(tcsetattr(channel,TCSANOW,&t))return 1;
#endif
    for(;;){char b[512],a[80],v[32],n[33],ssid[65],password[127],extra;unsigned long size;int i;
        if(!line(b,sizeof b)){if(transaction){if(!recover())return 1;cleanup();transaction=0;}backup_hash[0]=0;resume();
#ifdef SNMU_TEST
            return 0;
#else
            sleep(1);tcflush(channel,TCIFLUSH);continue;
#endif
        }
        int ok=0;if(sscanf(b,"SNMU1 INFO %32s %c",n,&extra)==1&&hex(n,32))ok=info(n);
        else if(!strcmp(b,"SNMU1 BACKUP")&&!transaction&&!backup_hash[0])ok=backup();
        else if(sscanf(b,"SNMU1 BEGIN %64s %31s %32s %c",a,v,n,&extra)==3)ok=begin(a,v,n);
        else if(sscanf(b,"SNMU1 PUT %d %lu %64s %c",&i,&size,a,&extra)==3)ok=put(i,size,a);
        else if(sscanf(b,"SNMU1 KEEP %d %64s %c",&i,a,&extra)==2)ok=keep(i,a);
        else if(!strcmp(b,"SNMU1 APPLY"))ok=apply();
        else if(sscanf(b,"SNMU1 WIFI %64s %126s %c",ssid,password,&extra)==2)ok=network(ssid,password);
        else if(!strcmp(b,"SNMU1 RESTART")&&!transaction&&applied){ok=reply("SNMU1 RESTARTED\n");
#ifndef SNMU_TEST
            sync();system("/sbin/reboot");
#endif
        }
        else if(!strcmp(b,"SNMU1 ABORT")){if(!recover())return 1;ok=cleanup();transaction=0;backup_hash[0]=0;resume();if(ok)ok=reply("SNMU1 ABORTED\n");}
        if(!ok){reply("SNMU1 ERROR usb_update_failed\n");if(transaction){if(!recover())return 1;cleanup();transaction=0;}backup_hash[0]=0;resume();}
    }
}

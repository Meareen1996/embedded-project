#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>
/* 非独占读取按键/触摸事件，不使用 EVIOCGRAB，不影响桌面。 */
int main(int argc, char **argv) {
    const char *device = "/dev/input/event2";
    long seconds = 10;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--sensor") && i+1<argc) {
            const char *s=argv[++i];
            if (!strcmp(s,"keys")) device="/dev/input/event2";
            else if (!strcmp(s,"touch")) device="/dev/input/event1";
            else { fprintf(stderr,"sensor: keys/touch\n"); return 2; }
        } else if (!strcmp(argv[i],"--count") && i+1<argc) {
            seconds=number(argv[++i],1,600);
        } else { fprintf(stderr,"Usage: %s [--sensor keys|touch] [--count seconds:1..600]\n",argv[0]); return 2; }
    }
    signal(SIGINT,stop_demo); signal(SIGTERM,stop_demo);
    setvbuf(stdout,NULL,_IOLBF,0);
    int fd=open(device,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) { perror(device); return 1; }
    char name[128]={0};
    if (ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0) { perror("input name"); close(fd); return 1; }
    const char *expected=!strcmp(device,"/dev/input/event2") ? "gpio-keys":"goodix-ts";
    if (strcmp(name,expected)) {
        fprintf(stderr,"Input mapping changed: %s is %s, expected %s\n",device,name,expected);
        close(fd); return 1;
    }
    fprintf(stderr,"%s %s: observe for %ld seconds; press USER key / touch screen. Nonexclusive read.\n",device,name,seconds);
    struct timespec start,now;
    clock_gettime(CLOCK_MONOTONIC,&start);
    int result=0;
    while (!stopped) {
        clock_gettime(CLOCK_MONOTONIC,&now);
        double elapsed=(now.tv_sec-start.tv_sec)+(now.tv_nsec-start.tv_nsec)/1e9;
        if (elapsed>=seconds) break;
        struct pollfd p={.fd=fd,.events=POLLIN};
        int rc=poll(&p,1,100);
        if (rc<0) { if (errno==EINTR) continue; perror("poll input"); result=1; break; }
        if (!rc) continue;
        if (p.revents&(POLLERR|POLLHUP|POLLNVAL)) { fprintf(stderr,"Input disconnected\n"); result=1; break; }
        struct input_event event;
        ssize_t n=read(fd,&event,sizeof(event));
        if (n<0 && (errno==EAGAIN || errno==EINTR)) continue;
        if (n!=(ssize_t)sizeof(event)) { fprintf(stderr,"Incomplete input event\n"); result=1; break; }
        printf("{\"type\":%u,\"code\":%u,\"value\":%d}\n",event.type,event.code,event.value);
    }
    close(fd); return result;
}

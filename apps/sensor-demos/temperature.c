#define _POSIX_C_SOURCE 200809L
#include "common.h"
int main(int argc, char **argv) {
    long count, interval; options(argc,argv,&count,&interval,NULL);
    /* 当前板卡的 thermal_zone0 已核对为 imx_thermal_zone。 */
    const char *root="/sys/class/thermal/thermal_zone0";
    char path[PATH_MAX],type[100]={0}; snprintf(path,sizeof(path),"%s/type",root);
    FILE *f=fopen(path,"r");
    if (!f) { perror(path); return 1; }
    int ok=fscanf(f,"%99s",type); fclose(f);
    if (ok!=1 || strcmp(type,"imx_thermal_zone")) {
        fprintf(stderr,"Expected imx_thermal_zone; check thermal mapping\n"); return 1;
    }
    fprintf(stderr,"i.MX6ULL die temperature, NOT room temperature.\n");
    for (long n=0; !stopped && (!count || n<count); ++n) {
        printf("{\"sample\":%ld,\"cpu_temperature_C\":%.3f}\n",n+1,read_value(root,"temp")/1000.0);
        if (!count || n+1<count) pause_ms(interval);
    }
    return 0;
}

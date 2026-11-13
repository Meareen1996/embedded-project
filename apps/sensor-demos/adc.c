#define _POSIX_C_SOURCE 200809L
#include "common.h"
int main(int argc, char **argv) {
    long count, interval; options(argc,argv,&count,&interval,NULL);
    char root[PATH_MAX]; find_iio("2198000.adc",root,sizeof(root));
    fprintf(stderr,"ADC: %s; channels may be unconnected. Values do not identify a sensor.\n",root);
    double scale = read_value(root,"in_voltage_scale"); /* IIO 电压 scale 单位 mV/LSB */
    for (long n=0; !stopped && (!count || n<count); ++n) {
        printf("{\"sample\":%ld",n+1);
        for (int c=0;c<5;++c) {
            char attr[40]; snprintf(attr,sizeof(attr),"in_voltage%d_raw",c);
            double raw = read_value(root,attr);
            printf(",\"ch%d_raw\":%.0f,\"ch%d_mV\":%.3f",c,raw,c,raw*scale);
        }
        puts("}");
        if (!count || n+1<count) pause_ms(interval);
    }
    return 0;
}

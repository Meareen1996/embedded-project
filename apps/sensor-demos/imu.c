#define _POSIX_C_SOURCE 200809L
#include "common.h"
int main(int argc, char **argv) {
    long count, interval; const char *sensor = "all";
    options(argc, argv, &count, &interval, &sensor);
    int accel = !strcmp(sensor,"all") || !strcmp(sensor,"accel");
    int gyro = !strcmp(sensor,"all") || !strcmp(sensor,"gyro");
    int temp = !strcmp(sensor,"all") || !strcmp(sensor,"temp");
    if (!accel && !gyro && !temp) { fprintf(stderr,"sensor: all/accel/gyro/temp\n"); return 2; }
    char root[PATH_MAX]; find_iio("icm20608", root, sizeof(root));
    fprintf(stderr,"ICM20608: %s (SPI -> kernel IIO -> this app).\n", root);
    if (temp) fprintf(stderr,"Old image temperature coefficients are suspect: emitting raw/scale/offset only, not calibrated Celsius.\n");
    for (long n=0; !stopped && (!count || n<count); ++n) {
        printf("{\"sample\":%ld", n+1);
        /* sysfs 各轴顺序读取，适合入门；不是同步 FIFO 采样。 */
        const char *types[] = {"accel", "anglvel"};
        const char *units[] = {"m_s2", "rad_s"};
        for (int t=0; t<2; ++t) if (t ? gyro : accel) {
            char attr[80];
            snprintf(attr,sizeof(attr),"in_%s_scale",types[t]);
            double scale = read_value(root,attr);
            for (int a=0; a<3; ++a) {
                snprintf(attr,sizeof(attr),"in_%s_%c_raw",types[t],'x'+a);
                double raw = read_value(root,attr);
                printf(",\"%s_%c_raw\":%.0f,\"%s_%c_%s\":%.6f",
                       types[t],'x'+a,raw,types[t],'x'+a,units[t],raw*scale);
            }
        }
        if (temp) printf(",\"imu_temp_raw\":%.0f,\"imu_temp_scale\":%.9f,\"imu_temp_offset\":%.0f",
                         read_value(root,"in_temp_raw"),read_value(root,"in_temp_scale"),read_value(root,"in_temp_offset"));
        puts("}");
        if (!count || n+1<count) pause_ms(interval);
    }
    return 0;
}

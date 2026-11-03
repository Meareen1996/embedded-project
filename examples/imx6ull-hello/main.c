#include <stdio.h>

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "world";
    printf("Hello, %s!\n", name);
    puts("This program was cross-compiled in Ubuntu and runs on the I.MX6ULL board.");
    return 0;
}

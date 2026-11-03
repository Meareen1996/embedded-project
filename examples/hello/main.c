#include <stdio.h>

static int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    // puts("你好，Yangmeiling！我的第一个 C 程序运行成功。");
    printf("a=%a,b=%b,result= %d\n", add(2, 3));
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include "epoll_class.h"







// 通过port端口进行通信
// port由命令行给出
int main(int argc,char* argv[])
{
    // 判断传入参数
    if(argc<=1)
    {
        printf("按照如下格式运行: %s port_number\n",basename(argv[0]));
        return -1;
    }

    // 获取端口号
    int port=atoi(argv[1]);

    epoll_class epoll_sample(port);
    epoll_sample.run();


    return 0;
}
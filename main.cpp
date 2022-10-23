#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/epoll.h>
#include "http_conn.h"
#include "locker.h"
#include "pthreadpool.h"

#define MAX_FD 65535            // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 最大的监听的事件数量


// 添加信号捕捉
void addsig(int sig,void(handler)(int))
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    // 应用信号捕捉
    sigaction(sig,&sa,NULL);
}

// 添加文件描述符到epoll
extern void addfd(int epollfd,int fd,bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd,int fd);
// 修改文件描述符
extern void modfd(int epollfd,int fd,int ev);




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

    // 对SIGPIPE信号处理
    // 不希望因为SIGPIPE退出
    addsig(SIGPIPE,SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool=NULL;
    try{
        pool=new threadpool<http_conn>;
    }
    catch(const char* msg){
        printf("error creat thread\n");
        
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn *users=new http_conn[ MAX_FD ];

    // 进行网络通信
    // 创建监听的套接字
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd==-1){
        perror("socket");
        exit(-1);
    }
    
    //设置端口复用(在绑定之前)
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    // 绑定
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons(port);
    int ret=bind(listenfd,(struct sockaddr*)&addr,sizeof(addr));
    if(ret==-1){
        perror("bind");exit(-1);
    }

    // 监听
    listen(listenfd,5);

    // 创建epoll对象和事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(100);

    // 将监听的文件描述符添加到epoll中
    addfd(epollfd,listenfd,false);
    http_conn::st_m_epollfd=epollfd;
    http_conn::st_m_usercount=0;

    // 进行监听
    while(true)
    {
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(num<0 && errno!=EINTR)
        {
            printf("epoll fail\n");
            break;
        }

        // 循环遍历事件数组
        for(int i=0;i<num;i++)
        {
            int sockfd=events[i].data.fd;

            if(events[i].data.fd==listenfd)
            {// 监听到客户端连接
                sockaddr_in client_addr;
                socklen_t client_addrlen=sizeof(client_addr);
                int connfd=accept(listenfd,(sockaddr*)&client_addr,&client_addrlen);

                // 判断文件描述符表是否满了
                if(http_conn::st_m_usercount>=MAX_FD)
                {
                    // 回写数据:服务器正忙
                    // 连接满了
                    close(connfd);
                    continue;
                }
                // 将新的客户的数据初始化,放到数组中
                users[connfd].init(connfd,client_addr);
            }
            // 不是连接类型
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {// 异常断开等错误事件
                users[sockfd].close_conn();           
            }
            else if(events[i].events & EPOLLIN)
            {
                if(users[sockfd].read()){
                    // 读完了
                    pool->append(users+sockfd);
                }
                else{
                    // 读失败了
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                if(users[sockfd].Write()){
                    // 写成功(无操作)
                    ;
                }
                else{
                    // 写失败了
                    users[sockfd].close_conn();
                }
            }
        }
    }


    // 结束后close
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;


    return 0;
}
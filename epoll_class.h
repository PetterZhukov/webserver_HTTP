#ifndef _EPOLL_CLASS_
#define _EPOLL_CLASS_

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
#include "socket_control.h"

class epoll_class:public base
{
public:
    epoll_class(int port);
    ~epoll_class();
    void run();
private:
    //================== const ====================
    // 最大的文件描述符个数
    static const int MAX_FD = 65535;
    // 最大的监听的事件数量
    static const int MAX_EVENT_NUMBER = 10000;

    //================== value ====================
    //线程池
    threadpool<http_conn> * pool;
    // 创建一个数组用于保存所有的客户端信息
    http_conn *users;
    //监听的套接字
    int listenfd;
    // 创建epoll对象和事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd;



    //================== function ====================
    // 添加信号捕捉
    void addsig(int sig, void(handler)(int));
    // 非阻塞的读,循环读取
    bool Read(http_conn &conn);
    // 非阻塞的读
    bool Write(http_conn &conn);
    bool Write();
};
// 添加信号捕捉
void epoll_class::addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    // 应用信号捕捉
    sigaction(sig,&sa,NULL);
}
// 非阻塞的读,循环读取
bool epoll_class::Read(http_conn &conn)
{
    int m_sockfd=conn.get_sockfd();
    int m_read_index=conn.get_read_index();
    char *m_read_buf=conn.get_read_buf();

    // 缓冲已满
    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }
    // 读取字节
    while(true)
    {
        int bytes_read=recv(m_sockfd,m_read_buf+m_read_index,READ_BUFFER_SIZE-m_read_index,0);
        if(bytes_read==-1){
            if(errno==EAGAIN || errno==EWOULDBLOCK)
                break;
            else
                return false;
        }
        else if(bytes_read==0){
            return false;
        }
        m_read_index+=bytes_read;
    }
    // 更新值
    //printf("read data:\n%s",m_read_buf);
    conn.set_read_index(m_read_index);
    return true;
}
// 非阻塞的写
bool epoll_class::Write(http_conn &conn)
{
    int m_sockfd = conn.get_sockfd();
    int bytes_to_send = conn.get_bytes_to_send();
    int bytes_have_send = conn.get_bytes_have_send();
    const int m_write_index = conn.get_write_index();
    char *m_write_buf = conn.get_write_buf();
    char *m_address_mmap = conn.get_address_mmap();
    iovec *m_iv = conn.get_iv();
    int m_iv_count = conn.get_iv_count();

    if(bytes_to_send==0){
        // 将要发送的字节为0
        modfd(epollfd,m_sockfd,EPOLLIN);
        conn.clear();
        return true;
    }
    while (1)
    {
        int ret = writev(m_sockfd, m_iv, m_iv_count);
        if (ret <= -1)
        {
            // 发送失败
            if (errno == EAGAIN)
            {// 重试
                modfd(epollfd, m_sockfd, EPOLLOUT);
                conn.set_bytes_to_send(bytes_to_send);
                conn.set_bytes_have_send(bytes_have_send);
                return true;
            }
            conn.unmap(); // 释放内存
            return false;
        }
        // 本次写成功
        // 维护还需发送字节数和已发送字节数
        bytes_have_send += ret;
        //bytes_to_send -= ret;

        //分散写第一部分是否写完
        if (bytes_have_send >= m_iv[0].iov_len)
        { // 第一部分写完了
            m_iv[1].iov_base = m_address_mmap + (bytes_have_send - m_write_index);
            m_iv[1].iov_len = bytes_to_send-bytes_have_send;
            m_iv[0].iov_len = 0;
        }
        else
        { // 第一部分还没写完
            m_iv[0].iov_base = (char*)(m_iv[0].iov_base)+ret;
            m_iv[0].iov_len -= ret;
        }

        // 发送结束
        if (bytes_to_send <=bytes_have_send)
        {   // 发送HTTP响应成功,释放内存
            conn.unmap();
            // 方便下次复用
            modfd(epollfd, m_sockfd, EPOLLIN);
            // 是否keep-alive
            if (conn.is_keepalive())
            {
                conn.clear();
                // 继续接受信息
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

epoll_class::epoll_class(int port)
{
    // 对SIGPIPE信号处理
    // 避免因为SIGPIPE退出
    addsig(SIGPIPE,SIG_IGN);

    // 创建线程池，初始化线程池
    pool=NULL;
    try{
        pool=new threadpool<http_conn>;
    }
    catch(const char* msg){
        printf("error:%s\n",msg);
        exit(-1);
    }

    // 创建一个数组用于保存所有的客户端信息
    users=new http_conn[ MAX_FD ];

    // 进行网络通信
    // 创建监听的套接字
    listenfd=socket(AF_INET,SOCK_STREAM,0);
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
    ret=listen(listenfd,5);
    if(ret==-1){
        perror("listen");exit(-1);
    }

    // 创建epoll
    epollfd=epoll_create(100);

    // 将监听的文件描述符添加到epoll中
    addfd(epollfd,listenfd,false);
    http_conn::st_m_usercount=0;
    http_conn::st_m_epollfd=epollfd;

}
void epoll_class::run()
{
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
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                
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
                if(Read(users[sockfd])){
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
                if(Write(users[sockfd])){
                    // 写成功
                    ;
                }
                else{
                    // 写失败了 / 写完不保持连接
                    users[sockfd].close_conn();
                }
            }
        }
    }
}
epoll_class::~epoll_class()
{
        // 结束后close
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
}



#endif
#ifndef _HTTP_CONN_H_
#define _HTTP_CONN_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <unordered_map>
#include <tuple>


using std::unordered_map;
using std::tuple;



class http_conn
{
public:
    //======================== static ============================
    static int st_m_epollfd;            // epoll fd
    static int st_m_usercount;          // 用户数量
    static const int READ_BUFFER_SIZE=2048;     // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE=1024;    // 写缓冲区的大小
    static const int FILENAME_MAXLEN=200;       // 文件名的最大长度
    
    //======================== 状态值 ============================
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE   : 当前正在分析请求行
        CHECK_STATE_HEADER        : 当前正在分析头部字段
        CHECK_STATE_CONTENT       : 当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    /*
        从状态机的三种可能状态，即行的读取状态，分别表示
        LINE_OK     :   读取到一个完整的行
        LINE_BAD    :   行出错
        LINE_OPEN   :   行数据尚且不完整
    */
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn() {}
    ~http_conn() {}
    
    // 处理客户端的请求
    void process(); 
    //初始化新接收的连接
    void init(int sockfd,const sockaddr_in &addr); 
    // 关闭这个对象的连接
    void close_conn();   
    // 非阻塞的读
    bool read();
    // 非阻塞的写
    bool Write();

 
private:
    //================== socket通信的值 ====================
    int m_sockfd;           //该HTTP连接的socket
    sockaddr_in m_address;  // 通信的socket地址

    //================== 读缓冲区 ====================
    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
    int m_read_index;       // 标识读缓冲区中以及读入的客户端数据的最后一个字节的下标

    int m_checked_index;    // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;       // 当前正在解析的行的起始位置

    //================== 写缓冲区 ====================
    char m_write_buf[WRITE_BUFFER_SIZE];    // 写缓冲区
    int m_write_index;       // 写缓冲区中待发送的字节数



    //================== 报文解析结果 ========================
    //================== 请求行分析结果 ====================
    char * m_url;           // 请求目标文件的文件名
    char * m_version;       // 协议版本,只支持 HTTP1.1
    METHOD m_method;        // 请求方法
    //================== 请求头分析结果 ====================
    char* m_host;           // 主机名
    int m_content_length;   // 描述HTTP消息实体的传输长度
    bool m_keepalive;       // HTTP请求是否要保持连接
    //================== 返回数据 ====================
    char m_filename[FILENAME_MAX]; // 客户请求的目标文件的完整目录,其内容为 root_directory+m_url
    struct stat m_file_stat;       // 目标文件的状态
    char *m_address_mmap;          // 客户请求的数据被mmap到的位置
    void unmap();               // 释放内存映射的空间

    // 初始化基本的数值
    void init_private();
    //================== 状态机 ====================
    CHECK_STATE m_check_state;  // 主状态机当前所处的位置
    //============== 主状态机 ===================
    HTTP_CODE process_read();                   // 解析HTTP请求
    HTTP_CODE parse_request_line(char *text);   // 解析HTTP请求首行
    HTTP_CODE parse_headers(char *text);        // 解析HTTP请求头
    HTTP_CODE parse_content_complete(char *text); // 解析HTTP请求内容
    HTTP_CODE do_request();                     // 对报文做具体的处理
    //================== 从状态机 ====================
    LINE_STATUS parse_line();

    //================== 写响应报文 ====================
    bool process_write(HTTP_CODE ret);              // 写响应报文
    bool add_response(const char *format, ...);     // 往写缓冲区中写数据
    bool add_status_line(int status, const char *title); // 添加状态行
    bool add_headers(int content_len, time_t time);     // 添加响应头部
    // 响应头部组件
    bool add_content_length(int content_len);   // content-length
    bool add_connection();      // keep_alive
    bool add_content_type();    // Content-Type
    bool add_date(time_t t);    // 发送时间
    bool add_blank_line();      // 空白结束行
    bool add_content(const char *content); // 添加响应正文
    
    // writev成员
    struct iovec m_iv[2]; // 存储分散写的内容,0为报文头,1为报文内容
    int m_iv_count;     // writev数量

    // 返回当前行的起始位置对应的指针
    char *get_line() { return m_read_buf + m_start_line; }
};


#endif
#include "http_conn.h"

//#define patse_message 0
#define check_write_header 0
// #define check_write_content 1
// #define show_read_data 1
#define process_read_result 1
#define mmap_print 1

//================== HTTP响应的状态信息 ====================
// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 用hashmap来降低代码量
unordered_map<int, tuple<int, const char *, const char *>> response_info{
    {http_conn::FILE_REQUEST, {200, ok_200_title, NULL}},
    {http_conn::BAD_REQUEST, {400, error_400_title, error_400_form}},
    {http_conn::FORBIDDEN_REQUEST, {403, error_403_title, error_403_form}},
    {http_conn::NO_RESOURCE, {404, error_404_title, error_404_form}},
    {http_conn::INTERNAL_ERROR, {500, error_500_title, error_500_form}},
};

//================== 初始化 ====================
int http_conn::st_m_epollfd=-1;
int http_conn::st_m_usercount=0;

//================== 静态数据 ====================
const char* root_directory="resources";



// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag=old_flag | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
}

// 添加需要监听的文件描述符到epoll
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    // EPOLLRDHUP判断是否挂起
    // event.events=EPOLLIN | EPOLLRDHUP;
    event.events=EPOLLIN  | EPOLLRDHUP;
    if(one_shot) 
        event.events |=EPOLLONESHOT;
    
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中删除文件描述符,并close文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,NULL);
    close(fd);
}

// 修改文件描述符,重置socket上EPOLLONESHOT事件,确保下次可读时被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//初始化新接收的连接
void http_conn::init(int sockfd,const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象
    // 添加文件描述符信息到epoll
    addfd(st_m_epollfd, m_sockfd, true);
    st_m_usercount++;

    init_private();
}

// 初始化连接
void http_conn::init_private(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;
    m_write_index=0;
    bytes_have_send=0;
    bytes_to_send=0;

    m_iv_count=0;
    m_url=NULL;
    m_method=GET;
    m_version=0;
    //HTTP 1.1 中默认启用Keep-Alive，如果加入"Connection: close "，才关闭
    m_keepalive=true;

    m_host=NULL;
    m_content_length=0;
    m_address_mmap=NULL;
    

    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_write_buf,WRITE_BUFFER_SIZE);
    bzero(&m_address,sizeof(m_address));
    bzero(m_filename,FILENAME_MAXLEN);

}

//关闭连接
void http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(st_m_epollfd, m_sockfd);
        m_sockfd = -1;    // 重置fd
        st_m_usercount--; // 维护cnt
    }
}

// 非阻塞的读
// 循环读取
bool http_conn::read()
{
    // 缓冲已满
    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }
    // 读取字节
    int bytes_read=0;
    while(true)
    {
        bytes_read=recv(m_sockfd,m_read_buf+m_read_index,READ_BUFFER_SIZE-m_read_index,0);
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
    #ifdef  show_read_data
        printf("\n读取到的数据 : \n       %s\n",m_read_buf);
    #endif
    return true;
}



// 对内存映射区进行释放
void http_conn::unmap()
{
    if(m_address_mmap)
    {
        int ret=munmap(m_address_mmap,m_file_stat.st_size);
        if(ret==-1)
            perror("mmap");
        m_address_mmap=NULL;
    }
}


// 非阻塞的写
bool http_conn::Write()
{
    if(bytes_to_send==0){
        // 将要发送的字节为0
        modfd(st_m_epollfd,m_sockfd,EPOLLIN);
        init_private();
        return true;
    }

    #ifdef check_write_header
        printf("write header=============\n%s\n",(char *)m_iv[0].iov_base);
    #endif

    #ifdef check_write_content
        if (m_iv_count > 1)
            printf("write content=============\n%s\n", (char *)m_iv[1].iov_base);
    #endif

    char * content=(char*)m_iv[1].iov_base;
    int content_size=m_iv[1].iov_len;
    while (1)
    {
        int ret = writev(m_sockfd, m_iv, m_iv_count);
        if (ret <= -1)
        {
            // 发送失败
            if (errno == EAGAIN)
            {
                // 重试
                modfd(st_m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap(); // 释放内存
            return false;
        }
        else
        {
            // 本次写成功
            // 维护还需发送字节数和已发送字节数
            bytes_have_send += ret;
            //bytes_to_send -= ret;

            //分散写第一部分是否写完
            if(bytes_have_send>=m_write_index)
            {// 第一部分写完了
                int offset=bytes_have_send-m_write_index;
                m_iv[1].iov_len=content_size-offset;
                m_iv[1].iov_base=content+offset;

                // 禁用第一部分
                m_iv[0].iov_len=0;
                m_iv[0].iov_base=NULL;
            }
            else
            {// 第一部分还没写完
                m_iv[0].iov_base = (char*)m_iv[0].iov_base + ret;
                m_iv[0].iov_len -= ret;
            }

            // 发送结束
            if (bytes_have_send >= bytes_to_send)
            {
                // 发送HTTP响应成功,释放内存
                unmap();
                // 方便下次复用
                modfd(st_m_epollfd, m_sockfd, EPOLLIN);
                // 是否keep-alive
                if (m_keepalive)
                {
                    init_private();
                    // 继续接受信息
                    return true;
                }
                else
                {
                    // main接收到false后会关闭连接
                    // ?在write里面写close会不会更简单直接
                    return false;
                }
            }
        }
    }
    unmap();
    printf("分支错误\n\n\n\n\n");
    return false;   // 以防万一的返回值
}

// 处理客户端的请求
// 业务逻辑
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret=process_read();
    #ifdef  process_read_result
        printf("process_read result : %d\n",read_ret);
    #endif
    if(read_ret==NO_REQUEST)
    {   // 读的没有问题,则修改fd,让其再次使用
        modfd(st_m_epollfd,m_sockfd,EPOLLIN);
        return;
    }

    // 生成响应报文
    bool write_ret = process_write( read_ret );
    if(!write_ret){
        close_conn();
    }
    
    // close之后时候要执行modfd
    modfd(st_m_epollfd,m_sockfd,EPOLLOUT);

}

//============== 主状态机 ===================
// 解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text=NULL;

    while((m_check_state==CHECK_STATE_CONTENT) && line_status==LINE_OK 
        || ((line_status=parse_line())==LINE_OK))
    {   // 解析到了一行完整的数据  或者解析到了请求体,也是完整的数据
        // 获取一行数据
        text=get_line();
        #ifdef patse_message
             printf("\n即将解析的数据: %s\n",text);
        #endif
        m_start_line=m_checked_index;
        //printf("got 1 http line : %s\n",text);
    
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret=parse_request_line(text);
                // 分析返回值
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret=parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST){
                    // 解析到完整的请求头
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret=parse_content_complete(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST){
                    // 解析到完整的请求头
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
// 解析HTTP请求行,获取请求方法 ,目标URL,HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 1.解析请求行
    // GET /index.html HTTP/1.1
    // 对上文进行解析

    // url
    m_url=strpbrk(text," \t");
    if(!m_url)
        return BAD_REQUEST;

    /*GET /index.html HTTP/1.1
         ^                    */

    // method
    *(m_url++) = '\0'; // GET\0/index.html HTTP/1.1
    char *method=text;
    if(strcasecmp(method,"GET")==0){
        m_method=GET;
    }
    else{
        return BAD_REQUEST;
    }

    //   GET\0
    //   /index.html HTTP/1.1
    // version
    m_version=strpbrk(m_url," \t");
    if(!m_version)
        return BAD_REQUEST;
    
    *(m_version++)='\0';
    //   GET\0
    //   /index.html\0
    //   HTTP/1.1
    // 版本分析
    if(strcasecmp(m_version,"HTTP/1.1")!=0)
        return BAD_REQUEST;

    // url分析
    /**
     * 这样的,需要去掉 http:// 这7个字符
     * http://192.168.110.129:10000/index.html
    */
    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        // 跳到第一个/的位置
        m_url=strchr(m_url,'/');
    }
    if(!m_url || m_url[0]!='/')
        return BAD_REQUEST;
    
    // 2.更新检测状态,检测完请求行以后需要检测头部
    m_check_state=CHECK_STATE_HEADER;

    // 3.return
    #ifdef patse_message
        printf("请求头解析成功\n    url:%s,version:%s,method:%s\n",m_url,m_version,method);
    #endif
    return NO_REQUEST;
}
// 解析HTTP请求头
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    #ifdef patse_message
        printf("分析请求头 : %s\n",text);
    #endif
    /**
     * "Connection:"
     * "Content-Length:"
     * "Host:"
     */ 
    // 被perse_line处理过后,若text为空行,说明请求头已经结束
    if(text[0]=='\0'){
        // 若HTTP请求有消息体(m_content_length!=0) 则继续读取消息体
        if(m_content_length != 0 )
        {
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则读完成
            return GET_REQUEST;
    }
    else if( strncasecmp( text, "Connection:", 11 ) == 0 ){
        text+=11;   // 去除key
        text+=strcspn(text," \t");   // 去除开头的空格和\t
        
        if(strcasecmp(text,"keep-alive")==0){
            m_keepalive=true;
        }
        else if(strcasecmp(text,"close")==0){
            m_keepalive=false;
        }
    }
    else if(strncasecmp( text, "Content-Length:", 15 )==0){
        text+=15;   // 去除key
        text+=strcspn(text," \t");   // 去除开头的空格和\t
        
        m_content_length=atol(text);
    }
    else if ( strncasecmp( text, "Host:", 5 ) == 0 ){
        text+=5;   // 去除key
        text+=strcspn(text," \t");   // 去除开头的空格和\t
        
        m_host=text;
    }
    else{
        #ifdef patse_message
            printf("解析失败,不知名的请求头: %s\n",text);
        #endif
    }
    return NO_REQUEST;
}
// 解析HTTP请求内容
http_conn::HTTP_CODE http_conn::parse_content_complete(char *text)
{
    // 根据m_content_length查看内容是否完全读入
    if(m_read_index >= (m_checked_index + m_content_length)){
        // 数据完整
        text[ m_content_length ]='\0';
        return GET_REQUEST;
    };
    // 返回不完整
    return NO_REQUEST;
}

/**
 * 在分析完成以后进行具体的处理
 */ 
http_conn::HTTP_CODE http_conn::do_request()
{
    // 更新
    int sumlen=strlen(m_url)+strlen(root_directory)+1;
    snprintf(m_filename,std::min((int)(FILENAME_MAXLEN),sumlen),"%s%s",root_directory,m_url);
    printf("m_filename :%s\n",m_filename);
    // m_filename = "resources" + "/xxxxxxx"

    // 获取文件相关信息
    int ret=stat(m_filename,&m_file_stat);
    if(ret==-1){
        perror("stat");
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 对文件操作
    // 只读方式打开
    int fd=open(m_filename,O_RDONLY);
    // 创建内存映射
    m_address_mmap = (char*) mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    #ifdef mmap_print
        printf("\nmmap :==================\n %s ,m_address_mmap\n", m_address_mmap);
    #endif
    close(fd);

    return FILE_REQUEST;    // 获取文件成功
}   

//================== 从状态机 ====================
http_conn::LINE_STATUS http_conn::parse_line()
{
    // 根据\r\n
    char temp;

    for(;m_checked_index < m_read_index;++m_checked_index)
    {
        temp=m_read_buf[m_checked_index];
        if(temp=='\r')
        {// 遇到'\r' 进行判断
            if((m_checked_index+1)==m_read_index){
                // '\r'为最后一个,说明有数据未完
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index+1]=='\n')
            {
                // 完整的一句,将 \r\n 变为 \0
                m_read_buf[m_checked_index++]='\0';
                m_read_buf[m_checked_index++]='\0';
                // 相当于 x x+1 = \0  then x+=2
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp=='\n'){
            if((m_checked_index>1 && m_read_buf[m_checked_index-1]=='\r'))
            {// 这次的第一个和上一次的最后一个是一个分隔
                m_read_buf[m_checked_index-1]='\0';
                m_read_buf[m_checked_index++]='\0';
                return LINE_OK;
            }
        }
    }
    return LINE_OPEN;
}


//================== 写入部分 ====================
// 往写缓冲区中写数据
bool http_conn::add_response(const char*format, ...)
{
    if(m_write_index >= WRITE_BUFFER_SIZE){
        return false;   // 已满
    }
    va_list args;
    va_start(args,format);
    int len=vsnprintf(m_write_buf+m_write_index,WRITE_BUFFER_SIZE-m_write_index-1,format,args);
    // vsnprintf 用法类似snprintf,输入的最大长度为__maxlen-1
    // 调用args和format进行可变参数输入
    // 返回值为若空间足够则输入的长度
    if(len > WRITE_BUFFER_SIZE-m_write_index-1){
        // 说明输入的字符溢出
        return false;
    }
    m_write_index+=len; // 更新写缓冲区的长度
    va_end(args);
    return true;     
}
// 添加状态行
bool http_conn::add_status_line(int status,const char*title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
// 添加响应头部
bool http_conn::add_headers(int content_len,time_t time)
{
    if(!add_content_length(content_len)) return false;
    if(!add_content_type()) return false;
    if(!add_connection()) return false;
    if(!add_date(time)) return false;
    // if(!) return false;
    if(!add_blank_line()) return false;
    return true;
}
// 响应头部组件
//      content-length
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n",content_len);
}
//      Content-Type
bool http_conn::add_content_type()
{
    // 虑区分是图片 / html/css
    char *format_file=strrchr(m_filename,'.');
    return add_response("Content-Type: %s\r\n", format_file == NULL ? "text/html" : (format_file + 1));
}
//      keep_alive / close
bool http_conn::add_connection()
{
    return add_response("Connection: %s\r\n", (m_keepalive == true) ? "keep-alive" : "close");
}
//      发送时间
bool http_conn::add_date(time_t t)
{
    char timebuf[50];
	strftime(timebuf, 80, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return add_response("Date: %s\r\n", timebuf);
}
//      空白结束行
bool http_conn::add_blank_line()
{
    return add_response("\r\n");
}
// 添加响应正文
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

//================== 生成返回的报文 ====================
bool http_conn::process_write(HTTP_CODE ret){
    /*
        NO_REQUEST : 请求不完整，需要继续读取客户数据
        GET_REQUEST : 表示获得了一个完成的客户请求
        BAD_REQUEST : 表示客户请求语法错误
        NO_RESOURCE : 表示服务器没有资源
        FORBIDDEN_REQUEST : 表示客户对资源没有足够的访问权限
        FILE_REQUEST : 文件请求,获取文件成功
        INTERNAL_ERROR : 表示服务器内部错误
        CLOSED_CONNECTION : 表示客户端已经关闭连接了
    */

    int status = std::get<0>(response_info[ret]);
    const char *title = std::get<1>(response_info[ret]);
    const char *form = std::get<2>(response_info[ret]);
    if (ret == FILE_REQUEST)
    {// OK,发送报文头和文件
        if(!add_status_line(status, title)) return false;
        if(!add_headers(m_file_stat.st_size, time(NULL))) return false; // 发送本地时间
        #ifdef check_write_header
            if (check_write_header)
            {
                printf("OK 的 报文头:\n");
                write(STDOUT_FILENO, m_write_buf, m_write_index);
            }
        #endif  
        m_iv[0].iov_base=m_write_buf;
        m_iv[0].iov_len=m_write_index;
        m_iv[1].iov_base=m_address_mmap;
        m_iv[1].iov_len=m_file_stat.st_size;
        m_iv_count=2;

        // 维护发送长度
        bytes_to_send = m_write_index + m_file_stat.st_size;

        return true;
    }
    else if(response_info.find(ret)!=response_info.end())
    {// 发送错误信息
        if(!add_status_line(status, title)) return false;
        if(!add_headers(strlen(form), time(NULL))) return false; // 发送本地时间
        
        m_iv[0].iov_base=m_write_buf;
        m_iv[0].iov_len=m_write_index;
        m_iv[1].iov_base=(char*)form;
        m_iv[1].iov_len=strlen(form)+1;
        m_iv_count=2;

        // 若使用add_content
        //if(!add_content(form)) return false;
        // m_iv[0].iov_base = m_write_buf;
        // m_iv[0].iov_len = m_write_index;
        // m_iv_count = 1;

        // 维护发送长度
        bytes_to_send = m_iv[0].iov_len + m_iv[1].iov_len;

        return true;
    }
    else 
        return false;
}
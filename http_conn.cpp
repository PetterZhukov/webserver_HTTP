#include "http_conn.h"
#define PRINT_DEBUG 1
#define show_read_data 1


//================== 初始化 ====================
int http_conn::st_m_epollfd=-1;
int http_conn::st_m_usercount=0;

//================== 静态数据 ====================
const char* root_directory="/home/master/webserver/resources";



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

    m_url=NULL;
    m_method=GET;
    m_version=0;
    m_keepalive=false;

    bzero(m_read_buf,READ_BUFFER_SIZE);
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
    if(show_read_data) printf("读取到的数据 : %s\n",m_read_buf);
    return true;
}

// 非阻塞的写
bool http_conn::write()
{
    printf("一次性写完数据\n");
    return true;
}

// 处理客户端的请求
// 业务逻辑
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret=process_read();
    printf("process_read result : %d\n",read_ret);
    if(read_ret==NO_REQUEST)
    {   // 读的没有问题,则修改fd,让其再次使用
        modfd(st_m_epollfd,m_sockfd,EPOLLIN);
        return;
    }

    // 生成响应报文



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
        if(PRINT_DEBUG) printf("\n即将解析的数据: %s\n",text);
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
    if(PRINT_DEBUG) printf("请求头解析成功\n    url:%s,version:%s,method:%s\n",m_url,m_version,method);
    return NO_REQUEST;
}
// 解析HTTP请求头
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if(PRINT_DEBUG) printf("分析请求头 : %s\n",text);
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
        if(PRINT_DEBUG) printf("解析失败,不知名的请求头: %s\n",text);
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
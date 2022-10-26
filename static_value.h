#ifndef _STATIC_VALUE_H_
#define _STATIC_VALUE_H_

#include <unordered_map>
#include <tuple>
using std::unordered_map;
using std::tuple;


class base
{
public:
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小
    static const int FILENAME_MAXLEN = 200;    // 文件名的最大长度

    //======================== 状态值 ============================
    // HTTP请求方法，这里只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE   : 当前正在分析请求行
        CHECK_STATE_HEADER        : 当前正在分析头部字段
        CHECK_STATE_CONTENT       : 当前正在解析请求体
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

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
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    /*
        从状态机的三种可能状态，即行的读取状态，分别表示
        LINE_OK     :   读取到一个完整的行
        LINE_BAD    :   行出错
        LINE_OPEN   :   行数据尚且不完整
    */
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    //================== HTTP响应的状态信息 ====================
    // 定义HTTP响应的一些状态信息
    const char *ok_200_title = "OK";
    const char *error_400_title = "Bad Request";
    const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
    const char *error_403_title = "Forbidden";
    const char *error_403_form = "You do not have permission to get file from this server.\n";
    const char *error_404_title = "Not Found";
    const char *error_404_form = "The requested file was not found on this server.\n";
    const char *error_500_title = "Internal Error";
    const char *error_500_form = "There was an unusual problem serving the requested file.\n";

    // 用hashmap来降低代码量
    unordered_map<int, tuple<int, const char *, const char *>> response_info{
        {FILE_REQUEST, {200, ok_200_title, NULL}},
        {BAD_REQUEST, {400, error_400_title, error_400_form}},
        {FORBIDDEN_REQUEST, {403, error_403_title, error_403_form}},
        {NO_RESOURCE, {404, error_404_title, error_404_form}},
        {INTERNAL_ERROR, {500, error_500_title, error_500_form}},
    };
};

#endif
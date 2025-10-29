# WebServer HTTP API 文档

> 本文档覆盖本仓库对外可用的公开类、函数与组件，包含概览、形参/返回说明与典型用例。示例均基于 Linux 环境（epoll + pthread）。

- 组件总览
  - 基础常量与枚举：`base`（见 `static_value.h`）
  - 套接字/epoll 辅助：`setnonblocking` / `addfd` / `removefd` / `modfd`（见 `socket_control.h`）
  - 线程同步：`mutex` / `cond` / `sem`（见 `locker.h`）
  - 泛型请求队列：`questqueue<T>`（见 `questqueue.h`）
  - 泛型线程池：`threadpool<T>`（见 `pthreadpool.h`）
  - HTTP 连接处理：`http_conn`（见 `http_conn.h`）
  - 事件主循环与服务器：`epoll_class`（见 `epoll_class.h`）

> 运行与快速上手请参考 `docs/USAGE.md`。

---

## 基础：`base`（`static_value.h`）

提供全局常量与 HTTP/解析相关枚举，并内置错误响应描述表。

- 常量
  - `READ_BUFFER_SIZE = 2048`
  - `WRITE_BUFFER_SIZE = 1024`
  - `FILENAME_MAXLEN = 200`
- 枚举
  - `METHOD { GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT }`
  - `CHECK_STATE { CHECK_STATE_REQUESTLINE, CHECK_STATE_HEADER, CHECK_STATE_CONTENT }`
  - `HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION }`
  - `LINE_STATUS { LINE_OK, LINE_BAD, LINE_OPEN }`
- 预置响应信息（节选）
  - `response_info[FILE_REQUEST] -> (200, "OK", nullptr)`
  - `response_info[NO_RESOURCE] -> (404, "Not Found", "The requested file was not found...\n")`

典型用途：作为其他类（如 `http_conn`, `epoll_class`）的基类提供统一常量/状态。

---

## 套接字/epoll 辅助（`socket_control.h`）

- `void setnonblocking(int fd)`
  - 将 `fd` 设置为非阻塞模式（`O_NONBLOCK`）。
- `void addfd(int epollfd, int fd, bool one_shot)`
  - 将 `fd` 以 `EPOLLIN | EPOLLRDHUP` 事件加入 `epollfd`；如 `one_shot==true`，附加 `EPOLLONESHOT`；并自动设置非阻塞。
- `void removefd(int epollfd, int fd)`
  - 从 `epollfd` 删除 `fd` 并 `close(fd)`。
- `void modfd(int epollfd, int fd, int ev)`
  - 使用 `EPOLL_CTL_MOD` 修改 `fd` 的事件为 `ev | EPOLLONESHOT | EPOLLRDHUP`。

示例：
```cpp
int efd = epoll_create1(0);
int fd  = ::socket(AF_INET, SOCK_STREAM, 0);
addfd(efd, fd, true);            // 注册读事件，one-shot
modfd(efd, fd, EPOLLOUT);        // 切换为可写
removefd(efd, fd);               // 从 epoll 删除并关闭
```

---

## 线程同步（`locker.h`）

- `class mutex`
  - `bool lock()` / `bool unlock()`：加锁/解锁。
- `class cond`
  - `bool wait(pthread_mutex_t *m)`：在外部互斥量上等待。
  - `bool signal()`：唤醒一个等待线程。
  - `bool broadcast()`：唤醒全部等待线程（当前实现等价于 `signal()`，如需广播可自行扩展）。
- `class sem`
  - 构造：`sem()`（初始值0）/ `sem(int num)`（初始值num）
  - `bool wait()`：P 操作，计数-1，为0则阻塞。
  - `bool post()`：V 操作，计数+1，唤醒等待线程。

示例：
```cpp
mutex m;
sem   s(0);

// 生产者
auto producer = [&]{ /* ... */ s.post(); };
// 消费者
auto consumer = [&]{ s.wait(); /* ...处理... */ };
```

---

## 请求队列：`questqueue<T>`（`questqueue.h`）

基于 `std::list<T*>` + `mutex` + `sem` 的有界阻塞队列。

- 构造：`questqueue(int max_queue)`（`max_queue > 0`）
- `bool push(T *quest)`
  - 满则返回 `false`，否则入队并 `post()` 通知。
- `T* pop()`
  - 阻塞等待（`wait()`）直至有任务；返回队首元素。

示例：
```cpp
questqueue<MyTask> q(1024);
q.push(new MyTask(/*...*/));
MyTask* t = q.pop();
```

---

## 线程池：`threadpool<T>`（`pthreadpool.h`）

通用固定大小线程池，工作线程循环从 `questqueue<T>` 取任务并调用 `T::process()`。

- 构造：`threadpool(int poolsize = 8, int maxquest = 1000)`
- 析构：结束标志置位并释放线程句柄。
- `bool append(T *quest)`：将任务入队（队满则 `false`）。

用法要点：
- `T` 必须提供 `void process()` 成员函数。
- 线程创建后立即 `pthread_detach`。

示例：
```cpp
struct Job { void process(){ /* 业务逻辑 */ } };
threadpool<Job> pool(8, 1000);
Job* j = new Job();
pool.append(j);
```

---

## HTTP 连接：`http_conn`（`http_conn.h`/`http_conn.cpp`）

单连接请求的解析与响应构造。面向 Proactor：IO（读/写）由外层事件循环驱动，`http_conn` 专注于解析与生成响应。

- 静态：
  - `static int st_m_epollfd`：共享 epoll fd。
  - `static int st_m_usercount`：当前连接数。
- 生命周期：
  - `void init(int sockfd, const sockaddr_in &addr)`：加入 epoll（`EPOLLONESHOT`），设置端口复用与初始状态。
  - `void clear()`：复位内部状态与缓冲（供复用）。
  - `void close_conn()`：从 epoll 移除并关闭 fd，连接计数-1。
- 处理流程：
  - `void process()`：主入口。调 `process_read()` 解析；若请求完整则 `process_write()` 生成响应并切换 `EPOLLOUT`。
- 读写（对外可见的只读接口/查询）：
  - 一系列 `get_*()` 访问器、`set_read_index(int)` 设值器。
  - 说明：头文件声明了 `bool Write()`，但当前版本的实际写入由 `epoll_class::Write(http_conn&)` 统一完成，该成员未在本仓库实现。

响应特性：
- 仅支持 `GET`；默认不开启 `Keep-Alive`，当请求 `Connection: keep-alive` 时保持连接。
- 文件根目录固定为 `resources/`（见 `http_conn.cpp` 中 `root_directory`）。
- 成功（200）时使用 `writev` 分散写：报文头 + `mmap` 的文件内容。

---

## 事件主循环：`epoll_class`（`epoll_class.h`）

封装服务器整体：监听、accept、读/写事件分发、线程池调度。

- 构造：`epoll_class(int port)`
  - 初始化：忽略 `SIGPIPE`、创建 `threadpool<http_conn>`（默认 8 线程/队列 1000）、监听 `port`、设置复用、加到 epoll。
- 运行：`void run()`
  - `epoll_wait` 事件循环：
    - `EPOLLIN`：非阻塞读（内部 `Read(http_conn&)`），成功则将连接对象指针提交到线程池，在线程中执行 `http_conn::process()`；
    - `EPOLLOUT`：非阻塞写（内部 `Write(http_conn&)`），完成后依据 keep-alive 决定是否关闭；
    - 异常事件：关闭连接；
    - 新连接：`accept` 后调用 `http_conn::init(connfd, client_addr)`。
- 析构：关闭 epoll/监听 fd，释放连接数组与线程池。

示例：
```cpp
#include "epoll_class.h"

int main(int argc, char** argv){
  int port = 8080;       // 或从命令行读取
  epoll_class server(port);
  server.run();
}
```

扩展建议：
- 可在 `epoll_class` 构造中将 `pool = new threadpool<http_conn>(threads, maxQueue);` 调整为期望规模。
- 若需自定义静态目录，修改 `http_conn.cpp` 的 `root_directory`。

---

## 常见问答

- Q：如何开启 Keep-Alive？
  - A：客户端请求头包含 `Connection: keep-alive` 时，服务端保持连接；否则写完即关。
- Q：为什么我看不到 `http_conn::Write()` 的实现？
  - A：当前发送逻辑集中在 `epoll_class::Write(http_conn&)`，`Write()` 原型保留但未实现，后续可移除或实现为转发。
- Q：如何增大线程池或请求队列？
  - A：修改 `epoll_class` 构造中线程池初始化参数，或在 `pthreadpool.h` 中调整默认值。

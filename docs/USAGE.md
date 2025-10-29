# 使用与示例

## 环境需求
- Linux（内核支持 epoll）
- g++（建议 9+），支持 `-pthread`

## 构建

### 直接编译
```bash
g++ -std=c++11 -O2 -Wall -pthread \
  main.cpp http_conn.cpp socket_control.cpp \
  -o webserver
```

> 说明：`epoll_class.h` 为头文件内联实现，无需单独编译。

### 运行
```bash
./webserver 8080
```
- 访问静态资源目录：`resources/`（可在 `http_conn.cpp` 中修改 `root_directory`）。
- 示例：
  - 浏览器访问 `http://127.0.0.1:8080/index.html`
  - 或使用 curl：
    ```bash
    curl -v http://127.0.0.1:8080/
    ```

## 典型用法

### 作为独立 Web 服务运行
```cpp
#include "epoll_class.h"
int main(int argc, char** argv){
  int port = (argc>1? atoi(argv[1]) : 8080);
  epoll_class server(port);
  server.run();
}
```

### 自定义线程池规模与队列长度
`epoll_class` 默认构造了 `threadpool<http_conn>(8, 1000)`。如需调整，可在 `epoll_class` 构造函数中改为：
```cpp
pool = new threadpool<http_conn>(16, 2000); // 16 线程，队列 2000
```

### 请求处理流程（Proactor 模型）
- 主线程仅做 IO 就绪事件分发：
  - `EPOLLIN`：读取 -> 任务入队 -> 线程池中调用 `http_conn::process()` 解析与准备响应
  - `EPOLLOUT`：非阻塞写出（`epoll_class::Write()`），按 keep-alive 决定是否关闭
- 解析与应答：仅支持 GET；使用 `writev` 分散写（头 + `mmap` 文件内容）

### Keep-Alive 与连接管理
- 默认短连接；当请求头 `Connection: keep-alive` 时保持连接并复用 `http_conn` 对象。

### 日志/调试
在 `http_conn.cpp` 顶部可按需取消注释调试宏（如 `print_writev_result`、`process_read_result` 等）以输出解析与发送细节。

## 组件示例

### socket_control（epoll 辅助）
```cpp
int efd = epoll_create1(0);
int fd  = ::socket(AF_INET, SOCK_STREAM, 0);
addfd(efd, fd, true);
modfd(efd, fd, EPOLLOUT);
removefd(efd, fd);
```

### 线程同步（mutex/cond/sem）
```cpp
mutex m; sem s(0);
// Producer
auto p = [&]{ /* produce */ s.post(); };
// Consumer
auto c = [&]{ s.wait(); /* consume */ };
```

### 线程池与请求队列
```cpp
struct Job { void process(){ /* ... */ } };
threadpool<Job> pool(8, 1000);
Job* j = new Job();
pool.append(j);
```

## 疑难解答
- 无法访问文件：确认 `resources/` 下目标文件存在，且对其他用户具备读权限（`S_IROTH`）。
- 404/403：分别表示资源不存在/权限不足；详见 `base::response_info`。
- 高并发调优：
  - 增大线程数与队列长度（见上文）
  - 调整 `listen()` backlog（在 `epoll_class` 构造中）
  - 配置内核参数（例如 `net.core.somaxconn`、`fs.file-max` 等）

# webserver_HTTP
使用了线程池，通过 epoll 实现的 Proactor 版本的 web 服务器。参考了游双老师的《Linux 高性能服务器编程》以及牛客网的《Linux 高并发服务器开发》课程。在自己复现的基础上进行模块的整合并添加一些小更改。所有代码拥有完备的注释。

访问的资源在同级目录 `resources/` 文件夹中。

## 文档
- API 文档：[`docs/API.md`](docs/API.md)
- 使用与示例：[`docs/USAGE.md`](docs/USAGE.md)

欢迎访问我对项目进行相关解读的专栏（CSDN）：[链接如下](https://blog.csdn.net/qq_53157334/category_12075219.html)

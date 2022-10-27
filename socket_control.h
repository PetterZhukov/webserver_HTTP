#ifndef _SOCKET_CONTROL_H_
#define _SOCKET_CONTROL_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// 设置文件描述符非阻塞
extern void setnonblocking(int fd);

// 添加需要监听的文件描述符到epoll
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll中删除文件描述符,并close文件描述符
extern void removefd(int epollfd, int fd);

// 修改文件描述符,重置socket上EPOLLONESHOT事件,确保下次可读时被触发
extern void modfd(int epollfd, int fd, int ev);

#endif
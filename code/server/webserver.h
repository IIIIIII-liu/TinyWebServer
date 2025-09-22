#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../timer/heaptimer.h"

#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"

#include "../http/httpconn.h"

/*
 * WebServer 顶层类：负责启动监听、管理 epoll、分发读写、
 * 管理连接超时（timer）、线程池、和数据库连接池等。
 */
class WebServer {
public:
    // 构造函数：一次性把常用配置传进来
    // 参数（按顺序）：
    //   port        : 监听端口
    //   trigMode    : 触发模式（ET/LT 组合），通常用一个 int 表示配置
    //   timeoutMS   : 连接超时时间（毫秒）
    //   OptLinger   : 是否启用 SO_LINGER（优雅关闭选项）
    //   sqlPort/sqlUser/sqlPwd/dbName : MySQL 配置
    //   connPoolNum : 数据库连接池大小
    //   threadNum   : 线程池大小（用于处理耗时任务）
    //   openLog/logLevel/logQueSize : 日志相关配置
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~WebServer();

    // 启动服务器主循环：初始化资源并进入事件循环
    void Start();

private:
    // ------ 初始化和辅助 ------
    // 初始化监听 socket（socket()/bind()/listen()）并设置选项
    bool InitSocket_(); // 初始化套接字

    // 根据 trigMode 设置 listenEvent_ / connEvent_ 的掩码（EPOLLIN/EPOLLET/EPOLLONESHOT 等）
    void InitEventMode_(int trigMode);

    // 接受新连接后把 client 加入管理（在 users_ 中创建 HttpConn 并注册到 epoll）
    void AddClient_(int fd, sockaddr_in addr);
  
    // ------ 事件分发（主循环中会调用这些） ------
    // 处理监听 socket 的可读事件（accept 新连接）
    void DealListen_();

    // 处理写事件：把 write 缓冲写到 socket（可能会短写，需处理）
    void DealWrite_(HttpConn* client);

    // 处理读事件：从 socket 读入到缓冲并解析请求，或把任务交给线程池
    void DealRead_(HttpConn* client);

    // 向 fd 发送一个简单的错误响应并关闭连接（用于协议错误等）
    void SendError_(int fd, const char* info);

    // 当客户端有活动时，延长/重置该连接的定时器，避免被超时回收
    void ExtentTime_(HttpConn* client);

    // 关闭连接并清理资源（从 epoll 删除、关闭 fd、移除 users_、取消定时器）
    void CloseConn_(HttpConn* client);

    // 把 I/O 事件封装为线程池要执行的任务（OnRead_/OnWrite_ 通常是提交给线程池的入口）
    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);

    // 真正的业务处理入口：解析请求、生成响应（可能在 worker 线程中执行）
    void OnProcess(HttpConn* client);

    // 最大支持的文件描述符数量（作为参考或预分配上限）
    static const int MAX_FD = 65536;

    // helper：把 fd 设置为非阻塞（ET 模式必须）
    static int SetFdNonblock(int fd);

    // ------ 配置状态 ------
    int port_;             // 监听端口
    bool openLinger_;      // 是否启用 SO_LINGER 优雅关闭
    int timeoutMS_;        // 连接超时时间（毫秒）
    bool isClose_;         // 服务器是否已经关闭标志（Start 的循环使用）
    int listenFd_;         // 监听 socket 的 fd
    char* srcDir_;         // 静态资源目录（例如网页文件根目录）
    
    // epoll 上的事件掩码：listen socket 的事件与 client socket 的事件
    uint32_t listenEvent_;  // 监听 socket 要关注的事件掩码（例如 EPOLLIN | EPOLLET）
    uint32_t connEvent_;    // 连接 socket 要关注的事件掩码（例如 EPOLLIN | EPOLLOUT | EPOLLET | EPOLLONESHOT）
   
    // 核心组件：定时器、线程池、epoll 封装、以及连接表
    std::unique_ptr<HeapTimer> timer_;        // 管理连接的超时回调
    std::unique_ptr<ThreadPool> threadpool_;  // 处理耗时任务（如请求解析、DB 操作）
    std::unique_ptr<Epoller> epoller_;        // epoll 封装，等待/分发事件

    // 连接表：以 fd 为键保存每个连接的 HttpConn 对象（注意：存对象时会有地址稳定性问题）
    std::unordered_map<int, HttpConn> users_;
};

#endif //WEBSERVER_H

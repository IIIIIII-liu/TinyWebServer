#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

class SqlConnPool {
public:
    static SqlConnPool *Instance();// 单例模式

    MYSQL *GetConn();// 获取连接
    void FreeConn(MYSQL * conn);// 将连接归还到连接池
    int GetFreeConnCount();// 获取空闲连接数

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);// 初始化
    void ClosePool();// 销毁所有连接

private:
    SqlConnPool() = default;
    ~SqlConnPool() { ClosePool(); }

    int MAX_CONN_;// 最大连接数

    std::queue<MYSQL *> connQue_;// 连接池
    std::mutex mtx_;// 互斥锁
    sem_t semId_;// 信号量
};

/* 资源在对象构造初始化 资源在对象析构时释放*/
class SqlConnRAII {
public:
    // 从 connpool 获取连接，并把连接地址写回到调用者提供的指针 sql
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();
        sql_ = *sql;
        connpool_ = connpool;
    }
    
    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};

#endif // SQLCONNPOOL_H

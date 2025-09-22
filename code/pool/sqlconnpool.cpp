#include "sqlconnpool.h"

SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

//获取连接
MYSQL* SqlConnPool::GetConn() {
    MYSQL *conn = nullptr;

    if(connQue_.empty()) {
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }

    sem_wait(&semId_);// P 操作
    {
        std::lock_guard<std::mutex> locker(mtx_);
        conn = connQue_.front();
        connQue_.pop();
    }

    return conn;
}
//归还连接
void SqlConnPool::FreeConn(MYSQL *conn) {
    assert(conn);
    {
        std::lock_guard<std::mutex> locker(mtx_);
        connQue_.push(conn);
    }
    sem_post(&semId_);// V 操作
}
//获取空闲连接数
int SqlConnPool::GetFreeConnCount() {
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}
//初始化    
void SqlConnPool::Init(const char* host, int port,
                       const char* user,const char* pwd, 
                       const char* dbName, int connSize) {
    assert(connSize > 0);

    for(int i = 0; i < connSize; ++i) {
        MYSQL *conn = nullptr;
        conn = mysql_init(conn);

        if(!conn) {
            LOG_ERROR("MySQL init error!");
            assert(conn);
        }

        conn = mysql_real_connect(conn, host, user, pwd, dbName, port, nullptr, 0);
        if(!conn) {
            LOG_ERROR("MySQL connect error!");
        }
        connQue_.push(conn);
    }

    MAX_CONN_ = connSize;
    sem_init(&semId_, 0, MAX_CONN_);// 初始化信号量
}
//销毁所有连接
void SqlConnPool::ClosePool() {
    std::lock_guard<std::mutex> locker(mtx_);
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    mysql_library_end();
}

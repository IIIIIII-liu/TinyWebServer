#include "log.h"
// 构造函数：初始化成员变量的默认值

Log::Log(){
    lineCount_ = 0;      // 当前日志文件已写入的行数
    isOpen_ = false;     // 日志是否已打开（默认未打开）
    level_ = 1;          // 日志默认等级（可由 init 覆盖）
    toDay_ = 0;          // 记录当前日志文件对应的日期（目前仅记录日-of-month）
    fp_ = nullptr;       // 文件指针，尚未打开
    path_ = nullptr;     // 日志目录路径（指针形式，注意生命周期问题）
    suffix_ = nullptr;   // 日志后缀（指针形式）
    MAX_LINES_ = MAX_LINES; // 每个日志文件最大行数（默认常量）
    isAsync_ = false;    // 是否异步写日志（默认为 false）
}

// 析构函数：程序结束或 Logger 被销毁时需要清理资源
Log::~Log(){
    // 如果存在后台写线程并且可 join，则进行关闭流程
    if (writeThread_ && writeThread_->joinable()) {
        // 试图让后台线程把队列剩余的任务处理完
        while (!deque_->empty()) {
            deque_->flush(); // 唤醒消费者（write 线程）处理队列中的任务
        }
        deque_->Close();      // 关闭阻塞队列，通知 pop 的线程尽快返回
        writeThread_->join(); // 等待写线程退出
    }

    // 关闭并释放日志文件
    if (fp_) {
        std::lock_guard<std::mutex> locker(mtx_); // 加锁保护文件操作
        flush();    // 把缓冲刷入磁盘
        fclose(fp_); // 关闭文件流
    }
}

// 初始化日志系统：设置路径、后缀、是否异步、启动写线程并打开文件
void Log::init(int level, const char* path, const char* suffix, int maxQueueCapacity) {
    isOpen_ = true;
    level_ = level;
    path_ = path;       // 注意：这里直接保存传入的指针，若 path 是临时字符串会产生悬空指针风险
    suffix_ = suffix;   // 推荐改为 std::string 保存副本以保证安全

    // 如果指定了队列容量 > 0，则启用异步模式
    if (maxQueueCapacity > 0) {
        isAsync_ = true;
        if (!deque_) {
            // 创建阻塞队列（容量为 maxQueueCapacity）
            std::unique_ptr<BlockQueue<std::string>> new_deque(new BlockQueue<std::string>(maxQueueCapacity));
            deque_ = std::move(new_deque);

            // 创建并启动写线程，线程函数为静态 FlushLogThread
            std::unique_ptr<std::thread> new_thread(new std::thread(FlushLogThread));
            writeThread_ = std::move(new_thread);
        }
    }
    else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    // 获取本地时间（注意 localtime 非线程安全）
    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer); // 非线程安全版本，建议使用 localtime_r
    struct tm t = *sysTime;                 // 这里把结果拷贝到局部变量 t，避免后续被覆盖

    // 生成日志文件名： path/YYYY_MM_DD + suffix
    char fileName[LOG_PATH_LEN] = {0};
    snprintf(fileName, LOG_PATH_LEN - 1, "%s/%04d_%02d_%02d%s",
             path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    
    toDay_ = t.tm_mday; // 仅记录日-of-month（存在跨月/跨年判断错误的隐患）

    {
        // 打开文件、或创建目录后打开（用锁保护文件打开操作）
        std::lock_guard<std::mutex> locker(mtx_);
        if (fp_) {
            flush();
            fclose(fp_);
        }

        fp_ = fopen(fileName, "a"); // 以追加方式打开
        if (fp_ == nullptr) {
            // 如果打开失败，尝试创建目录（**注意**：这里传入的是 fileName 的目录部分应为 path_，
            // 但原代码对 path_ 做 mkdir，需要确保 path_ 指向目录而不是带文件名的字符串）
            mkdir(path_, 0777);      // 仅在目录不存在时调用（此处没有检测返回值）
            fp_ = fopen(fileName, "a"); // 再次尝试打开
        }
        assert(fp_ != nullptr); // 调试断言：文件打开失败则程序中断；生产环境建议做更稳健的错误处理
    }
}

// 懒汉式单例：局部静态对象（C++11 起局部静态变量初始化是线程安全的）
Log* Log::Instance() {
    static Log inst;
    return &inst;
}

// 静态线程入口：异步写线程会在这里调用对象的 AsyncWrite_
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}

// 异步写日志线程函数：不断从队列 pop 日志并写入文件
void Log::AsyncWrite_() {
    std::string str = "";
    // pop() 返回 false 表示队列关闭或退出条件；循环直到无法再 pop 为止
    while (deque_->pop(str)) {
        std::lock_guard<std::mutex> locker(mtx_); // 写文件要保护 fp_
        fputs(str.c_str(), fp_); // 将字符串写入文件（未做错误检查）
        // 注意：这里没有对 lineCount_、切文件等逻辑进行处理，可能需要在此处增加文件滚动逻辑
    }
}

// 将输出内容整理为日志条并写入（或入队）
void Log::write(int level, const char *format, ...) {
    // 获取当前时间（精确到微秒）
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec); // 非线程安全，建议 localtime_r
    struct tm t = *sysTime;                // 拷贝一份，避免后续被覆盖
    va_list valst;                         // 可变参数列表

    // 第一段：检查是否需要滚动日志文件（按天或按行）
    {
        std::lock_guard<std::mutex> locker(mtx_); // 加锁检查/切换文件，防止并发修改 fp_ 等
        // 日志不是今天，或行数达到阈值时需要切文件
        if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES_ == 0))) {
            char newFile[LOG_PATH_LEN];
            char tail[36] = {0};
            // tail 使用 toDay_ 的拆分（注意 toDay_ 存储格式决定拆分方式）
            snprintf(tail, 36, "%s.%04d_%02d_%02d%s",
                     path_, toDay_ / 10000, toDay_ % 10000 / 100, toDay_ % 100, suffix_);
            if(toDay_ != t.tm_mday) {
                // 新的一天：用新的文件名
                snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
                toDay_ = t.tm_mday;
                lineCount_ = 0;
            }
            else {
                // 按行数滚动：生成带序号的文件名
                snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s.%d%s", path_, tail, (lineCount_ / MAX_LINES_), suffix_);
            }
            // 把缓冲刷入磁盘并切换文件
            flush();
            fclose(fp_);
            fp_ = fopen(newFile, "a");
            assert(fp_ != nullptr);
            // 注意：此处没有对 fopen 失败做充分处理（仅 assert），生产环境应当有容错
        }
    }

    // 第二段：格式化日志内容并写入 buffer 或队列
    {
        std::lock_guard<std::mutex> locker(mtx_); // 持锁保护 buff_ 与 fp_ 的并发访问

        lineCount_++; // 增加行计数（注意：此处是提前增加，如果后续写入失败计数会不准确）

        // 写入时间前缀到缓冲区（假定缓冲区有至少 48 字节可写）
        int n = snprintf(buff_.BeginWrite(), 48, "%04d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff_.HasWritten(n); // 推进写位置
        AppendLogLevelTitle_(level); // 追加日志级别字符串（比如 [info]）

        // 格式化可变参数（日志内容）写入 buffer 的可写区域
        va_start(valst, format);
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, valst);
        va_end(valst);
        buff_.HasWritten(m); // 推进写位置（注意：若 m 大于 WritableBytes，表示被截断，应检查并处理）

        // 追加换行（原代码把 '\0' 也写入，这里保留原行为但建议只写 '\n'）
        buff_.Append("\n\0", 2);

        // 如果异步模式且队列存在且未满，则把日志字符串移入队列由后台线程写入
        if (isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr()); // RetrieveAllToStr 会返回字符串并清空 buff_
        }
        else {
            // 否则同步方式直接写入文件（fputs 在锁内，会阻塞写线程）
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();    // 清空缓冲区，准备下一条日志
    }
}

// 在日志中追加级别前缀（如 [debug] / [info] / [warn] / [error]）
void Log::AppendLogLevelTitle_(int level) {
    switch (level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

// flush：唤醒异步队列消费者并把文件流缓冲刷到磁盘
void Log::flush() {
    if (isAsync_) {
        deque_->flush(); // 唤醒消费者处理剩余消息（BlockQueue::flush 实现为 notify_one）
    }
    std::lock_guard<std::mutex> locker(mtx_);
    if (fp_) {
        fflush(fp_); // 把 stdio 缓冲区数据写到磁盘（或底层文件）
    }
}

// 获取日志等级（线程安全）
int Log::GetLevel() {
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

// 设置日志等级（线程安全）
void Log::SetLevel(int level) {
    std::lock_guard<std::mutex> locker(mtx_);   
    level_ = level;
}

#ifndef BUFFER_H
#define BUFFER_H

#include <cstring>   // perror, memcpy
#include <iostream>
#include <unistd.h>  // write, close
#include <sys/uio.h> // readv
#include <vector>    // 底层存储
#include <atomic>
#include <assert.h>

/*
 * Buffer
 *
 * 简要说明：
 * 这是一个基于 std::vector<char> 的字节缓冲区类，用于网络 IO 场景。
 * 它维护读写两个索引（readPos_, writePos_），并提供：
 *  - 可读字节数（ReadableBytes）
 *  - 可写字节数（WritableBytes）
 *  - 前置可用字节数（PrependableBytes）
 *  常见用法：
 *   - 从 socket 读到 Buffer（ReadFd）
 *   - 从 Buffer 写到 socket（WriteFd）
 *   - 解析请求使用 Peek() + Retrieve(...)
 *   - 发送响应使用 Append(...)，随后 WriteFd()
 *
 * 设计注意：
 *  - 本类并未在内部对复杂并发场景做全锁保护；若在多线程环境共享同一 Buffer，
 *    需要外部加锁或使用专门的无锁 SPSC 设计。尽管使用了 std::atomic 来存位置索引，
 *    但复杂操作（扩容、移动内存、多个字段联合操作）仍需同步保护。
 */

class Buffer {
public:
    // 构造：initBuffSize 为初始缓冲区容量（字节）
    // 推荐保留少量前置空间（例如用于 prepend header），但此处由实现者决定。
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;
    /* ----------------- 大小/空间相关 ----------------- */

    // 返回当前可写入的字节数（buffer_ 末尾到 writePos_ 的剩余容量）
    // 公式通常为 buffer_.size() - writePos_
    size_t WritableBytes() const;

    // 返回当前可读取的字节数（已写入但未被 读取的字节数）
    // 公式通常为 writePos_ - readPos_
    size_t ReadableBytes() const ;

    // 返回前置可用的字节数（readPos_ 的值）
    // 通常用于在缓冲区前部预留协议头空间
    size_t PrependableBytes() const;

    /* ----------------- 访问/写入/推进接口 ----------------- */

    // 返回指向当前可读数据起始位置的 const 指针（不移动 readPos_）
    // 注意：当 buffer 扩容时该指针可能失效，使用时需小心
    const char* Peek() const;

    // 确保缓冲区至少有 len 字节的可写空间；若不足则扩容或搬移数据
    // （不会改变 writePos_、readPos_，只是保证 BeginWrite() 可安全写入 len 字节）
    void EnsureWriteable(size_t len);

    // 标记已经向缓冲区写入 len 字节（通常在直接写入 BeginWrite() 后调用）
    // 会推进 writePos_ += len
    void HasWritten(size_t len);

    /* ----------------- 读取/回收接口 ----------------- */

    // 将已读走 len 字节（推进 readPos_），len <= ReadableBytes()
    // 若 len == ReadableBytes()，可选择将 readPos_ 和 writePos_ 重置为 0（实现决定）
    void Retrieve(size_t len);

    // 将 readPos_ 移动到 end 指针处（end 必须指向 buffer 内部的某个位置）
    void RetrieveUntil(const char* end);

    // 清空所有可读数据（相当于 Retrieve(ReadableBytes())）
    // 实现可选择不释放底层 capacity，仅将索引重置
    void RetrieveAll();

    // 将所有可读数据拷贝成 std::string 返回，并清空缓冲区
    // 注意：会做一次拷贝，性能敏感场景请优先使用 Peek() + Retrieve()
    std::string RetrieveAllToStr();

    /* ----------------- 写入指针/数据追加 ----------------- */

    // 返回 const 指针，指向当前可写起始处（read-only 视图）
    const char* BeginWriteConst() const;

    // 返回可写起始处的非 const 指针（可直接写入，然后调用 HasWritten）
    char* BeginWrite();

    // 以下一组 Append 将数据追加到缓冲区尾部（会自动 EnsureWriteable）
    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff); // 将另一个 Buffer 的可读内容复制过来

    /* ----------------- 与文件描述符交互（IO） ----------------- */

    // 从 fd 读取数据追加到 buffer 中（通常用于 socket 读取）
    // 实现常用 readv：第一个 iovec 指向 BeginWrite() 的可写空间，
    // 第二个 iovec 指向临时栈缓冲区以接收溢出数据，然后把溢出数据 Append 进来。
    // 返回读到的字节数（>=0），若 <0 出错并把 errno 写入 *Errno
    ssize_t ReadFd(int fd, int* Errno);

    // 把 Buffer 中的可读数据写入 fd（通常用于 socket 发送）
    // 处理部分写、EINTR、EAGAIN/EWOULDBLOCK 等情形；写出的字节数返回 >=0，
    // 出现严重错误返回 -1 并把 errno 写入 *Errno。
    ssize_t WriteFd(int fd, int* Errno);

private:
    /* ----------------- 内部辅助函数 ----------------- */

    // 返回底层缓冲区开始的指针（非 const 版本）
    char* BeginPtr_();

    // 返回底层缓冲区开始的指针（const 版本）
    const char* BeginPtr_() const;

    // 当可写空间不足时调用：尝试复用前面读取掉的空间（移动未读数据到头部），
    // 如果仍不足则扩容 vector（通常按增长策略扩容）
    void MakeSpace_(size_t len);

    /* ----------------- 成员变量 ----------------- */

    std::vector<char> buffer_;            // 底层存储：连续内存块
    std::atomic<std::size_t> readPos_;    // 读取索引（已读取的位置）
    std::atomic<std::size_t> writePos_;   // 写入索引（已写入的位置）
    // 说明：尽管使用了 std::atomic 来存索引，但复杂操作（如扩容、MakeSpace_）仍需外部同步，
    // 因此本类默认**不保证多生产者/多消费者并发安全**。若在多线程间共享 Buffer，
    // 请在外层使用互斥（mutex）或采用单生产者-单消费者（SPSC）模型并仔细处理内存序列。
};

#endif //BUFFER_H

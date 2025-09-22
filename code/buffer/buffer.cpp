#include "buffer.h"

//初始化下标
Buffer::Buffer(int initBuffSize): buffer_(initBuffSize),readPos_(0), writePos_(0) {}

//可写字节数
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}
//可读字节数
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}
//前置可用字节数
size_t Buffer::PrependableBytes() const {
    return readPos_;
}
//返回可读数据的起始位置
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}
//确保缓冲区有足够的可写空间
void Buffer::EnsureWriteable(size_t len) {  
  if(WritableBytes() < len) {
      MakeSpace_(len);
  }
  assert(WritableBytes() >= len);
}
//标记已经写入len字节
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}
//将已读走len字节
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
    if(readPos_ == writePos_) {
        readPos_ = 0;
        writePos_ = 0;
    }
}
//将readPos_移动到end指针处
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end);
    assert(end <= BeginPtr_() + writePos_);
    Retrieve(end - Peek()); 
}
//清空所有可读数据
void Buffer::RetrieveAll() {
    readPos_ = 0;
    writePos_ = 0;
}
//将所有可读数据拷贝成string返回，并清空缓冲区
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}
//返回可写起始处的const指针
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}
//返回可写起始处的非const指针
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_; 
}
//将字符串追加到缓冲区尾部
void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}
//将字符数组追加到缓冲区尾部
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    Append(static_cast<const void*>(str), len);
}
//将任意数据追加到缓冲区尾部
// void Buffer::Append(const void* data, size_t len) {
//     assert(data);
//     EnsureWriteable(len);
//     std::copy(data, data + len, BeginWrite());    // 将str放到写下标开始的地方
//     HasWritten(len);
// }
void Buffer::Append(const void* data, size_t len){
    EnsureWriteable(len); // 先确保有足够空间（如果你有这个函数）
    const char* d = static_cast<const char*>(data);
    std::copy(d, d + len, BeginWrite());
    HasWritten(len); // 通常需要推进 writePos_
}


//将另一个Buffer的可读内容复制过来
void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}
//返回底层缓冲区开始的指针（非const版本）
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}
//返回底层缓冲区开始的指针（const版本）
const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}
//当可写空间不足时调用：尝试复用前面读取掉的空间
void Buffer::MakeSpace_(size_t len) {
    if(WritableBytes() + PrependableBytes() < len) { //连前置空间也不够
        buffer_.resize(writePos_ + len + 1); //多加1字节，防止writePos_==buffer_.size()
    } else {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_()); //把未读数据搬到缓冲区头部
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}
//从fd读取数据追加到buffer中
ssize_t Buffer::ReadFd(int fd, int* Errno) {
    char buff[65536]; //栈上临时缓冲区
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);
    const ssize_t len = readv(fd, iov, 2);
    if(len < 0) {
        *Errno = errno;
    } else if(static_cast<size_t>(len) <= writable) {
        writePos_ += len;
    } else {
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}
//把Buffer中的可读数据写入fd
ssize_t Buffer::WriteFd(int fd, int* Errno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *Errno = errno; 
        return len;
    }
    readPos_ += len;
    if(readPos_ == writePos_) {
        readPos_ = 0;
        writePos_ = 0;
    }
    return len;
}
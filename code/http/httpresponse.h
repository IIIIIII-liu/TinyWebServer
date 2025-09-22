#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);// 初始化
    void MakeResponse(Buffer& buff);// 生成响应报文
    void UnmapFile();// 解除文件映射
    char* File();// 获取文件地址
    size_t FileLen() const;// 获取文件长度
    void ErrorContent(Buffer& buff, std::string message);// 将错误信息添加到响应报文
    int Code() const { return code_; }// 获取响应状态码

private:
    void AddStateLine_(Buffer &buff);
    void AddHeader_(Buffer &buff);
    void AddContent_(Buffer &buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;// 状态码
    bool isKeepAlive_;// 是否保持连接

    std::string path_;// 请求路径
    std::string srcDir_;// 站点根目录

    char* mmFile_; // mmap文件指针
    struct stat mmFileStat_;// 文件状态

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  // 后缀类型集
    static const std::unordered_map<int, std::string> CODE_STATUS;          // 编码状态集
    static const std::unordered_map<int, std::string> CODE_PATH;            // 编码路径集
};


#endif //HTTP_RESPONSE_H

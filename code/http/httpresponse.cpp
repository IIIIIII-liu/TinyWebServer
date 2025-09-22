#include "httpresponse.h"

//构造函数
HttpResponse::HttpResponse() {
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}
//析构函数
HttpResponse::~HttpResponse() {
    UnmapFile();
}

//初始化
void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive, int code) {
    assert(srcDir != "");
    if (mmFile_) { UnmapFile(); }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    srcDir_ = srcDir;
    path_ = path;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}

//生成响应报文
void HttpResponse::MakeResponse(Buffer& buff) {
    //判断请求资源文件
    if (stat((srcDir_ + path_).c_str(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
    } else if (!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    } else if (code_ == -1) {
        code_ = 200;
    }
    ErrorHtml_();//生成错误页面
    //添加状态行、消息报头、响应正文
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}
//解除文件映射
void HttpResponse::UnmapFile() {
    if (mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);   
        mmFile_ = nullptr;
    }
}
//获取文件地址
char* HttpResponse::File() {
    return mmFile_;
}
//获取文件长度
size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}
//将错误信息添加到响应报文
void HttpResponse::ErrorContent(Buffer& buff, std::string message) {
    std::string body;
    std::string status;
    if (CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        status = "Bad Request";
    }
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    body += std::to_string(code_) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em> Liu's Web Server</em></body></html>";
    buff.Append("Content-Length: " + std::to_string(body.size()) + "\r\n");
    buff.Append("Content-Type: text/html\r\n");
    buff.Append("\r\n");
    buff.Append(body);
}
//添加状态行
void HttpResponse::AddStateLine_(Buffer &buff) {
    std::string status;
    if (CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}
//添加消息报头
void HttpResponse::AddHeader_(Buffer &buff) {
    buff.Append("Connection: ");
    if (isKeepAlive_) {
        buff.Append("keep-alive\r\n");
        buff.Append("Keep-Alive: max=6, timeout=120\r\n");
    } else {
        buff.Append("close\r\n");
    }
    buff.Append("Content-Type: " + GetFileType_() + "\r\n");
}
//添加响应正文
void HttpResponse::AddContent_(Buffer &buff) {
    int srcFd = open((srcDir_ + path_).c_str(), O_RDONLY);
    if (srcFd < 0) {
        ErrorContent(buff, "File Not Found!");
        return;
    }
    //将文件映射到内存提高文件的访问速度
    LOG_DEBUG("file path %s", (srcDir_ + path_).c_str());
    mmFile_ = (char*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if (mmFile_ == MAP_FAILED) {
        ErrorContent(buff, "File Not Found!");
        close(srcFd);   
    }
    close(srcFd);
    buff.Append("Content-Length: " + std::to_string(mmFileStat_.st_size) + "\r\n");
    buff.Append("\r\n");
}
//生成错误页面
void HttpResponse::ErrorHtml_() {
    if (CODE_PATH.count(code_) == 1) {
        path_ = CODE_PATH.find(code_)->second;
        if (stat((srcDir_ + path_).c_str(), &mmFileStat_) < 0) {
            code_ = 404;
        }
    }
}
//获取文件类型
std::string HttpResponse::GetFileType_() {
    std::string::size_type idx = path_.find_last_of('.');
    if (idx != std::string::npos) {
        std::string suffix = path_.substr(idx);
        if (SUFFIX_TYPE.count(suffix) == 1) {
            return SUFFIX_TYPE.find(suffix)->second;
        }
    }
    return "text/plain";
}
//状态码与状态信息
const std::unordered_map<int, std::string> HttpResponse::CODE_STATUS = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {500, "Internal Server Error"},
};
//状态码与错误路径
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    {400, "/400.html"},
    {403, "/403.html"},
    {404, "/404.html"},
    {500, "/500.html"},
};
//后缀与文件类型
const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"}, 
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css"},
    {".js", "text/javascript"},
};
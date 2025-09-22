#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>    // 可能用于解析请求行或 header（注意：std::regex 在某些实现上性能较差）
#include <errno.h>     
#include <mysql/mysql.h>  // mysql C API（UserVerify 用到）

#include "../buffer/buffer.h"   // 自定义环形/字节缓冲区，用于从 socket 读入的数据解析
#include "../log/log.h"         // 日志工具
#include "../pool/sqlconnpool.h"// MySQL 连接池（线程安全）用于用户验证等操作

// HttpRequest：用于解析单个 HTTP 请求（面向单连接/单线程的请求对象）
// 设计职责：增量解析 HTTP 请求（支持粘包/拆包），把请求行/头部/请求体解析成可访问的字段。
// 使用方式示例：
//   HttpRequest req; req.Init();
//   while (从 socket 读到数据放入 Buffer) {
//       if (req.parse(buffer)) { // 解析完成 -> 访问 req.method(), req.path(), req.GetPost("k") 等
//           // 构造响应...
//           req.Init(); // 若连接 keep-alive，重用对象解析下一个请求
//       }
//   }
class HttpRequest {
public:
    // 解析状态机的几个状态：
    // REQUEST_LINE: 正在解析请求行（例如 "GET /index.html HTTP/1.1"）
    // HEADERS:      正在解析请求头部（每行 "Key: value"）
    // BODY:         正在读取请求体（POST 时，依据 Content-Length）
    // FINISH:       请求已解析完成
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    
    HttpRequest() { Init(); }   // 构造时初始化状态
    ~HttpRequest() = default;

    // 重置对象到初始状态（用于复用同一对象解析下一个请求）
    // - 清空 method_, path_, version_, body_
    // - state_ = REQUEST_LINE
    // - 清空 header_、post_
    void Init();

    // 从 Buffer 中增量解析请求。函数通常会被上层循环调用，直到返回 true（解析完成）或发生错误。
    // 返回：true 表示请求已完整解析（state_ == FINISH）
    //       false 表示尚未完成（或解析出错——具体可在实现中区分）
    bool parse(Buffer& buff);   

    // 访问器：返回请求路径（例如 "/index.html"）
    std::string path() const;
    // 非 const 版本（极少用）：允许修改 path_
    std::string& path();

    // 返回请求方法，例如 "GET"、"POST"
    std::string method() const;
    // 返回 HTTP 版本字符串，例如 "1.1"
    std::string version() const;

    // 从解析好的 POST 参数中获取键值（若不存在返回空字符串）
    // 注意：只有当请求体是 urlencoded（application/x-www-form-urlencoded）并且已经被 ParsePost_ 解析后，post_ 才有值
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    // 判断是否使用长连接（keep-alive）
    // 实现应参考 HTTP 版本与 Connection 头（HTTP/1.1 默认 keep-alive 除非 Connection: close）
    bool IsKeepAlive() const;

private:
    // 以下为解析各部分的内部方法（由 parse 调用）
    // 返回 true/false 取决于解析是否成功（例如请求行格式错误则返回 false）
    bool ParseRequestLine_(const std::string& line);    // 处理请求行
    void ParseHeader_(const std::string& line);         // 处理单条请求头
    void ParseBody_(const std::string& line);           // 处理请求体（当前实现按整块 body 处理）

    // 路径相关处理（例如把 "/" 映射为 "/index.html"，把 "/index" 映射为 "/index.html"）
    void ParsePath_();

    // 处理 POST 请求：根据 Content-Type 解析 body_（例如 urlencoded）
    void ParsePost_();

    // 从 "application/x-www-form-urlencoded" 格式解析键值对并放入 post_ map（会做 URL 解码）
    void ParseFromUrlencoded_();

    // 用户校验：访问 MySQL 连接池，检查用户名密码或插入新用户（登录/注册）
    // 参数：
    //   name  - 用户名
    //   pwd   - 密码（注意：示例项目通常是明文比较，但真实项目请使用哈希+salt）
    //   isLogin - true: 校验登录；false: 注册时检查是否可用并插入
    // 返回：校验/注册是否成功（true/false）
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    // 当前解析状态
    PARSE_STATE state_;
    // 基本请求字段
    std::string method_, path_, version_, body_;
    // 存储 header 的键值（key 不区分大小写最好在插入时统一转换为小写）
    std::unordered_map<std::string, std::string> header_;
    // POST 表单解析结果（key -> value）
    std::unordered_map<std::string, std::string> post_;

    // 默认的静态页面集合（例如访问 "/index" 时会映射到 "/index.html"）
    static const std::unordered_set<std::string> DEFAULT_HTML;
    // 默认页面的 tag（例如 register.html 对应一个 tag，用于判断注册/登录逻辑）
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;

    // 把十六进制字符（'0'..'9','a'..'f','A'..'F'）转为整数 0..15
    // 返回 -1 表示非法 hex 字符（实现中应判断返回值）
    static int ConverHex(char ch);  // 16进制转换为10进制
};

#endif

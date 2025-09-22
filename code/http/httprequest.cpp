#include "httprequest.h"
// 初始化
void HttpRequest::Init() {
    method_ = ""; 
    path_ = ""; 
    version_ = "";
    body_ = "";
    state_ = REQUEST_LINE; 
    header_.clear();
    post_.clear();
}
// 解析处理
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    if (buff.ReadableBytes() <= 0) {
        return false;
    }

    // 解析循环：根据当前状态，逐步解析请求行、头部、请求体
    while (buff.ReadableBytes() > 0&& state_ != FINISH) {
        // 获取一行数据（以 CRLF 结尾）
        const char* line_end = std::search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        if (line_end == buff.BeginWrite()) {
            // 没有找到完整的一行，等待更多数据
            break;
        }
        std::string line(buff.Peek(), line_end);
        buff.RetrieveUntil(line_end + 2); // 移动读指针，跳过 CRLF
        if(state_ == REQUEST_LINE) {
            // 解析请求行
            if (!ParseRequestLine_(line)) {
                return false; // 请求行格式错误
            }
            state_ = HEADERS; // 切换到解析头部状态
        } else if (state_ == HEADERS) {
            // 解析请求头
            if (line.empty()) {
                // 空行表示头部结束
                if (header_.count("Content-Length")) {
                    state_ = BODY; // 有请求体，切换到解析体状态
                } else {
                    state_ = FINISH; // 无请求体，解析完成
                }
            } else {
                ParseHeader_(line); // 解析单条头部
            }
        } else if (state_ == BODY) {
            // 解析请求体
            ParseBody_(line);
            state_ = FINISH; // 解析完成
        }
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}
// 解析路径
void HttpRequest::ParsePath_() {
    if (path_ == "/") {
        path_ = "/index.html"; // 默认首页
    } else {
        // 查找是否有默认页面映射
        size_t last_slash = path_.find_last_of('/');
        std::string last_part = path_.substr(last_slash);
        if (DEFAULT_HTML.count(last_part)) {
            path_ = path_ + ".html"; // 映射为 .html 页面
        }
    }
}
// 处理请求行
bool HttpRequest::ParseRequestLine_(const std::string& line){
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    // 正则匹配请求行
    if (regex_match(line, subMatch, patten)) {
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        // 仅支持 GET 和 POST 方法
        if (method_ == "GET" || method_ == "POST") {
            ParsePath_(); // 处理路径映射
            return true;
        }
    }
    return false;
}
// 处理请求头
void HttpRequest::ParseHeader_(const std::string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if (regex_match(line, subMatch, patten)) {
        std::string key = subMatch[1];
        std::string value = subMatch[2];
        header_[key] = value; // 存储头部键值对
    }
}
// 处理请求体
void HttpRequest::ParseBody_(const std::string& line) {
    body_ = line;
    ParsePost_(); // 解析 POST 请求
}
// 解析 POST 请求
void HttpRequest::ParsePost_() {
    if (method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_(); // 仅处理 urlencoded 格式
        if(DEFAULT_HTML_TAG.count(path_)) { // 如果是登录/注册的path
            int tag = DEFAULT_HTML_TAG.find(path_)->second; 
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);  // 为1则是登录
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                } 
                else {
                    path_ = "/error.html";
                }
            }
        }
    }
}
// 解析 urlencoded 格式的请求体
void HttpRequest::ParseFromUrlencoded_() {
    if (body_.empty()) {
        return;
    }
    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;
    while (i < n) {
        char ch = body_[i];
        switch (ch) {
            case '=':
                key = body_.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                body_[i] = ' '; // '+' 转为空格
                break;
            case '%':
                num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
                body_[i + 2] = num % 10 + '0';
                body_[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = body_.substr(j, i - j);
                j = i + 1;
                post_[key] = value; // 存储键值对
                key = value = "";
                break;
            default:
                break;
        }
        ++i;
    }
    // 处理最后一个键值对
    if (key.empty() && j < i) {
        key = body_.substr(j, i - j);
        post_[key] = "";
    } else if (!key.empty() && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}
// 十六进制转整数
int HttpRequest::ConverHex(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }       
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1; // 非法字符
}
// 用户校验
bool HttpRequest::UserVerify(const std::string& name, const std::string& pwd, bool isLogin) {
    if (name.empty() || pwd.empty()) {
        return false;
    }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql = nullptr;
    // SqlConnRAII(&sql, SqlConnPool::Instance());
    SqlConnRAII conn(&sql, SqlConnPool::Instance());
    assert(sql);
    bool flag = false;
    unsigned int j = 0;
    char order[256] = {0};
    MYSQL_RES* res;
    if (isLogin) {
        snprintf(order, 256, "SELECT password FROM user WHERE username='%s' LIMIT 1", name.c_str());
        LOG_INFO("Login Query: %s", order);
        if (mysql_query(sql, order)) {
            mysql_free_result(res);
            return false;
        }
        res = mysql_store_result(sql);
        // 查询结果只有一行一列
        while ((res != nullptr) && (j = mysql_num_rows(res)) > 0) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row[0] && pwd == row[0]) {
                flag = true; // 密码匹配
            } else {
                flag = false; // 密码不匹配
            }
        }
        mysql_free_result(res);
    } else { // 注册用户
        snprintf(order, 256, "SELECT username FROM user WHERE username='%s' LIMIT 1", name.c_str());
        LOG_INFO("Register Query: %s", order);
        if (mysql_query(sql, order)) {// 查询失败
            mysql_free_result(res);
            return false;
        }
        // 获取查询结果
        res = mysql_store_result(sql);
        if (mysql_num_rows(res) == 0) {
            // 用户名不存在，可以注册
            snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s', '%s')", name.c_str(), pwd.c_str());
            LOG_INFO("Insert Query: %s", order);
            if (mysql_query(sql, order)) {
                mysql_free_result(res);
                return false;
            }
            flag = true; // 注册成功
        } else {
            flag = false; // 用户名已存在   
        }
        mysql_free_result(res);
    }
    return flag;
}   
// 获取请求方法
std::string HttpRequest::method() const {
    return method_;
}
// 获取请求路径
std::string HttpRequest::path() const {
    return path_;
}
// 非 const 版本，允许修改 path_
std::string& HttpRequest::path() {
    return path_;
}
// 获取 HTTP 版本
std::string HttpRequest::version() const {
    return version_;
}
// 获取 POST 参数值
std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if (post_.count(key)) {
        return post_.find(key)->second;
    }
    return "";
}
std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if (post_.count(key)) {
        return post_.find(key)->second;
    }
    return "";
}
// 判断是否为长连接
bool HttpRequest::IsKeepAlive() const {
    if (header_.count("Connection")) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login", "/welcome", "/video", "/picture", "/favicon.ico"
};
const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0},
    {"/login.html", 1}
};
#ifndef RXTRACENETCAP_LEGACY_CORE_H
#define RXTRACENETCAP_LEGACY_CORE_H

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <deque>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <inttypes.h>
#include <libgen.h>
#include <list>
#include <map>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <tr1/memory>
#include <unistd.h>
#include <vector>

#if __cplusplus >= 201103L
#include <memory>
namespace compat {
    using std::shared_ptr;
    using std::weak_ptr;
    using std::unique_ptr;
    using std::enable_shared_from_this;
    using std::static_pointer_cast;
    using std::dynamic_pointer_cast;
    using std::const_pointer_cast;
    using std::make_shared;
}
#else
namespace compat {
    using std::tr1::shared_ptr;
    using std::tr1::weak_ptr;
    using std::tr1::enable_shared_from_this;
    using std::tr1::static_pointer_cast;
    using std::tr1::dynamic_pointer_cast;
    using std::tr1::const_pointer_cast;
    template<typename T>
    inline std::tr1::shared_ptr<T> make_shared() {
        return std::tr1::shared_ptr<T>(new T());
    }
    template<typename T, typename A0>
    inline std::tr1::shared_ptr<T> make_shared(const A0& a0) {
        return std::tr1::shared_ptr<T>(new T(a0));
    }
    template<typename T, typename A0, typename A1>
    inline std::tr1::shared_ptr<T> make_shared(const A0& a0, const A1& a1) {
        return std::tr1::shared_ptr<T>(new T(a0, a1));
    }
    template<typename T, typename A0, typename A1, typename A2>
    inline std::tr1::shared_ptr<T> make_shared(const A0& a0, const A1& a1, const A2& a2) {
        return std::tr1::shared_ptr<T>(new T(a0, a1, a2));
    }
}
#endif

using compat::shared_ptr;
using compat::weak_ptr;
using compat::enable_shared_from_this;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

#define DAFAULT_EPOLL_SIZE 1000
#define DEFAULT_EPOLL_WAITE 10
const int MAX_RECV_SIZE = 1024*20;
const int MAX_SEND_NUM = 5;

const uint32_t MAX_HTTP_HEAD_LEN = 100*1024;

static const uint32_t SIZE_LEN_32 = 32;
static const uint32_t SIZE_LEN_128 = 128;
static const uint32_t SIZE_LEN_256 = 256;
static const uint32_t SIZE_LEN_1024 = 1024;
static const uint32_t SIZE_LEN_32768 = 32768;

typedef signed char  int8_t;
typedef unsigned char  uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;

#define CHANNEL_MSG_TAG "c"

#define OBJ_ID_THREAD 1
#define OBJ_ID_DOMAIN 2

#define OBJ_ID_BEGIN 1000

#define TIMER_ID_BEGIN 1000

#define MAX_CHANNEL_EVENT_TIMEOUT  1000

#define CRLF "\r\n"
#define CRLF2 "\r\n\r\n"

#define NORMAL_MSG_CONNECT 1
#define NORMAL_MSG_HTTP_REPLY 2

#define NONE_TIMER_TYPE 1
#define DELAY_CLOSE_TIMER_TYPE 2
#define WEB_SOCKET_HANDSHAKE_OK_TIMER_TYPE 3
#define DOMAIN_CACHE_TIMER_TYPE 5
#define NONE_DATA_TIMER_TYPE 6
#define NONE_CHANNEL_EVENT_TIMER_TYPE 7

#ifdef DEBUG
#define PDEBUG(format, arg...) \
do { \
    printf("tid:[%lu],line:[%d] ,func:[%s],file:[%s] \n" format, pthread_self(), __LINE__, __func__, __FILE__, ##arg); \
} while (0)
#else
#define PDEBUG(format, arg...)
#endif

enum LogLevel {
    LOG_FATAL = 1,
    LOG_WARNING = 2,
    LOG_NOTICE = 4,
    LOG_TRACE = 8,
    LOG_DEBUG = 16
};

enum LogLevelMask {
    LOG_LEVEL_FATAL   = LOG_FATAL,
    LOG_LEVEL_WARNING = LOG_FATAL | LOG_WARNING,
    LOG_LEVEL_NOTICE  = LOG_FATAL | LOG_WARNING | LOG_NOTICE,
    LOG_LEVEL_TRACE   = LOG_FATAL | LOG_WARNING | LOG_NOTICE | LOG_TRACE,
    LOG_LEVEL_DEBUG   = LOG_FATAL | LOG_WARNING | LOG_NOTICE | LOG_TRACE | LOG_DEBUG
};

class LogStream;

class Logger {
    friend class LogStream;
public:
    static Logger& getInstance();

    void init(const char* log_path = "logs",
              const char* prefix = NULL,
              unsigned int max_file_size = 100 * 1024 * 1024,
              int log_level = LOG_DEBUG);

    void init_from_path(const char* config);
    void init_from_path(const std::string& config) { init_from_path(config.c_str()); }

    void writeLog(LogLevel level, const char* file, int line, const char* func, const char* format, ...);

    void setLogLevel(int level);

    const char* getLevelName(LogLevel level);

private:
    Logger();
    ~Logger();

    Logger(const Logger&);
    Logger& operator=(const Logger&);

    void checkRotateFile(LogLevel level);

    std::string getLogFilePath(LogLevel level);

    std::string getTimestamp();

    bool createDirectory(const std::string& path);

private:
    std::string log_path_;
    std::string prefix_;
    unsigned int max_file_size_;
    int log_level_;
    pthread_mutex_t mutex_;
    bool initialized_;

    FILE* log_files_[32];
};

class LogStream {
public:
    LogStream(LogLevel level, int line, const char* func, const char* file);
    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T& data) {
        ss_ << data;
        return *this;
    }

    std::string str() const;

private:
    std::stringstream ss_;
    LogLevel level_;

    LogStream(const LogStream&);
    LogStream& operator=(const LogStream&);
};

#define LOG_INIT_WITH_ARGS(path, prefix, max_size, level) \
    Logger::getInstance().init(path, prefix, max_size, level)

#define LOG_INIT_SELECT(_1,_2,_3,_4,NAME,...) NAME
#define LOG_INIT_1ARG(path) \
    Logger::getInstance().init_from_path(path)
#define LOG_INIT_4ARGS(path, prefix, max_size, level) \
    LOG_INIT_WITH_ARGS(path, prefix, max_size, level)
#define LOG_INIT_INVALID(...) \
    Logger::getInstance().init_from_path(NULL)
#define LOG_INIT(...) \
    LOG_INIT_SELECT(__VA_ARGS__, LOG_INIT_4ARGS, LOG_INIT_INVALID, LOG_INIT_INVALID, LOG_INIT_1ARG)(__VA_ARGS__)

#define LOG_SET_LEVEL(level) \
    Logger::getInstance().setLogLevel(level)

#define LOG_FATAL_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_WARNING_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_ERROR_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_NOTICE_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_NOTICE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_TRACE_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_DEBUG_MSG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_FATAL_STREAM LogStream(LOG_FATAL, __LINE__, __func__, __FILE__)
#define LOG_WARNING_STREAM LogStream(LOG_WARNING, __LINE__, __func__, __FILE__)
#define LOG_NOTICE_STREAM LogStream(LOG_NOTICE, __LINE__, __func__, __FILE__)
#define LOG_TRACE_STREAM LogStream(LOG_TRACE, __LINE__, __func__, __FILE__)
#define LOG_DEBUG_STREAM LogStream(LOG_DEBUG, __LINE__, __func__, __FILE__)

#define LOG_FATAL(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_WARNING(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_ERROR(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_WARNING, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_NOTICE(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_NOTICE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_TRACE(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_TRACE, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        Logger::getInstance().writeLog(LOG_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    } while(0)

class reload_inf
{
    public:
        virtual ~reload_inf() {}
        virtual int load() = 0;
        virtual int reload() = 0;
        virtual bool need_reload()=0;
        virtual int dump() = 0;
        virtual int destroy() = 0;
};

template<typename T>
class reload_mgr: public reload_inf
{
    public:
        reload_mgr(T * T1, T *T2);
        virtual ~reload_mgr();

        int load();

        int reload();

        T* current();

        bool need_reload();

        int dump();

        int destroy();

        T* idle();

    private:
        T * _objects[2];
        int16_t _curr;
};

template<typename T>
reload_mgr<T>::reload_mgr(T * T1, T *T2)
{
    _objects[0] = T1;
    _objects[1] = T2;
    _curr = 0;
}

template<typename T>
reload_mgr<T>::~reload_mgr()
{
    if (_objects[0])
    {
        _objects[0]->destroy();
        delete _objects[0];
        _objects[0] = NULL;
    }

    if (_objects[1])
    {
        _objects[1]->destroy();
        delete _objects[1];
        _objects[1] = NULL;
    }
}

template<typename T>
int reload_mgr<T>::load()
{
    if( _objects[_curr]->load() == 0 )
    {
        return 0;
    }

    return -1;
}

template<typename T>
bool reload_mgr<T>::need_reload()
{
    return current()->need_reload();
}

template<typename T>
int reload_mgr<T>::reload()
{
    if ( _objects[1 - _curr]->reload() == 0 )
    {
        _curr = 1 - _curr;
        return 0;
    } else
    {
        PDEBUG("reload data failed,%d", _curr);
        return -1;
    }

    return 0;
}

template<typename T>
T* reload_mgr<T>::current() {
    if( _curr == 0 || _curr == 1){
        return (_objects[_curr]);
    }

    return NULL;
}

template<typename T>
int reload_mgr<T>::dump()
{
    reload_inf* obj = current();
    return obj->dump();
}

template<typename T>
int reload_mgr<T>::destroy()
{
    if (_objects[0])
        _objects[0]->destroy();

    if (_objects[1])
        _objects[1]->destroy();

    return 0;
}

template<typename T>
T* reload_mgr<T>::idle()
{
    if( _curr == 0 || _curr == 1){
        return (_objects[1 - _curr]);
    }

    return NULL;
}

class CRxThreadMutex {
public:
    CRxThreadMutex();
    ~CRxThreadMutex();
    void lock();
    void unlock();
private:
    pthread_mutex_t _mutex;
};

class CRxThreadLock {
public:
    CRxThreadLock(CRxThreadMutex * mutex);
    ~CRxThreadLock();
private:
    CRxThreadMutex * _mutex;
};

class CRxThreadRwlock {
public:
    CRxThreadRwlock();
    ~CRxThreadRwlock();
    void read_lock();
    void write_lock();
    void unlock();

private:
    pthread_rwlock_t _rwlock;
};

class CRxReadLock {
public:
    CRxReadLock(CRxThreadRwlock * rwlock) : _rwlock(rwlock) {
        if (_rwlock) {
            _rwlock->read_lock();
        }
    }
    ~CRxReadLock() {
        if (_rwlock) {
            _rwlock->unlock();
        }
    }
private:
    CRxThreadRwlock * _rwlock;
    CRxReadLock(const CRxReadLock&);
    CRxReadLock& operator=(const CRxReadLock&);
};

class CRxWriteLock {
public:
    CRxWriteLock(CRxThreadRwlock * rwlock) : _rwlock(rwlock) {
        if (_rwlock) {
            _rwlock->write_lock();
        }
    }
    ~CRxWriteLock() {
        if (_rwlock) {
            _rwlock->unlock();
        }
    }
private:
    CRxThreadRwlock * _rwlock;
    CRxWriteLock(const CRxWriteLock&);
    CRxWriteLock& operator=(const CRxWriteLock&);
};

struct ObjId
{
    uint32_t _id;
    uint32_t _thread_index;

    ObjId():_id(0), _thread_index(0){}
};

bool operator < (const ObjId & oj1, const ObjId & oj2);

bool operator==(const ObjId & oj1, const ObjId & oj2);

class normal_msg : public enable_shared_from_this<normal_msg>
{
    public:
        normal_msg(){}
        virtual ~normal_msg(){}
        normal_msg(int op):_msg_op(op){}
        int _msg_op;
};

class content_msg: public normal_msg
{
    public:
        content_msg()
        {
            _msg_op = NORMAL_MSG_CONNECT;
            fd = 0;
        }

        virtual ~content_msg(){}
        int fd;
};

struct timer_msg
{
        timer_msg()
        {
            _timer_type = NONE_TIMER_TYPE;
            _time_length = 0;
            _timer_id = 0;
            _obj_id = 0;
        };

        ~timer_msg()
        {

        }

        uint32_t _obj_id;
        uint32_t _timer_type;
        uint32_t _timer_id;
        uint32_t _time_length;
};

struct url_info
{
    std::string url;
    std::string protocol_type;
    std::string domain;
    std::string ip;
    int port;
    std::string full_path;
    std::string path;
    std::string query;

    void reset()
    {
        url.clear();
        protocol_type.clear();
        domain.clear();
        ip.clear();
        port = 80;
        full_path.clear();
        path.clear();
        query.clear();
    }
};

enum http_cmd_type {
    HTTP_REQ_GET     = 1 << 0,
    HTTP_REQ_POST    = 1 << 1,
    HTTP_REQ_HEAD    = 1 << 2,
    HTTP_REQ_PUT     = 1 << 3,
    HTTP_REQ_DELETE  = 1 << 4,
    HTTP_REQ_OPTIONS = 1 << 5,
    HTTP_REQ_TRACE   = 1 << 6,
    HTTP_REQ_CONNECT = 1 << 7,
    HTTP_REQ_PATCH   = 1 << 8
};

enum HTTP_STATUS
{
    RECV_HEAD = 0,
    RECV_BODY = 1,
    SEND_HEAD = 2,
    SEND_BODY = 3
};

struct http_response_code
{
        http_response_code()
        {
            _response_list.insert(std::make_pair(200, "OK"));
            _response_list.insert(std::make_pair(206, "Partial Content"));
            _response_list.insert(std::make_pair(301, "Moved Temporarily"));
            _response_list.insert(std::make_pair(302, "the uri moved temporarily"));
            _response_list.insert(std::make_pair(304, "page was not modified from las"));
            _response_list.insert(std::make_pair(400, "Bad Request"));
            _response_list.insert(std::make_pair(404, "Not Found"));
            _response_list.insert(std::make_pair(403, "Forbidden"));
            _response_list.insert(std::make_pair(409, "Conflict"));
            _response_list.insert(std::make_pair(500, "Internal Server Error"));
            _response_list.insert(std::make_pair(501, "not implemented"));
            _response_list.insert(std::make_pair(503, "the server is not available"));
        }

        ~http_response_code()
        {
        }

        std::string get_response_str(int status_code);

        std::map<int, std::string> _response_list;
        static http_response_code response;
};

struct set_cookie_item
{
    std::string _value;
    std::string _path;
    std::string _domain;
    uint64_t _expire;
    set_cookie_item()
    {
        _expire = 0;
    }
};

struct http_req_head_para
{
    http_req_head_para()
    {
        _method = "GET";
        _version = "HTTP/1.1";
    }

    void init()
    {
        _method.clear();
        _url_path.clear();
        _version.clear();

        _headers.clear();
        _cookie_list.clear();
    }

    std::string * get_header(const char * str)
    {
        std::string * ptr = NULL;
        if (!str) {
            return ptr;
        }

        std::map<std::string, std::string>::iterator it;
        for (it = _headers.begin(); it != _headers.end(); it++) {
            if (strcasestr(it->first.c_str(), str)) {
                ptr = &(it->second);
                break;
            }
        }

        return ptr;
    }

    void to_head_str(std::string * head);

    std::string _method;
    std::string _url_path;
    std::string _version;

    std::map<std::string, std::string> _headers;
    std::map<std::string, std::string> _cookie_list;
};

struct net_addr
{
    std::string ip;
    int port;

    net_addr()
    {
        port = 0;
    }
};

struct http_res_head_para
{
    http_res_head_para()
    {
        _response_code = 200;
        _response_str = "OK";
        _version = "HTTP/1.1";
    }

    void init()
    {
        _response_code = 200;
        _response_str.clear();
        _version.clear();
        _cookie_list.clear();
        _headers.clear();
        _chunked.clear();
    }

    std::string * get_header(const char * str)
    {
        std::string * ptr = NULL;
        if (!str) {
            return ptr;
        }

        std::map<std::string, std::string>::iterator it;
        for (it = _headers.begin(); it != _headers.end(); it++) {
            if (strcasestr(it->first.c_str(), str)) {
                ptr = &(it->second);
                break;
            }
        }

        return ptr;
    }

    void to_head_str(std::string * head);

    int _response_code;
    std::string _response_str;
    std::string _version;
    std::map<std::string, set_cookie_item> _cookie_list;
    std::map<std::string, std::string> _headers;
    std::string _chunked;
};

enum HTTP_RECV_TYPE
{
    CHUNK_TYPE = 0,
    CONTENT_LENGTH_TYPE = 1,
    OTHER_TYPE = 2
};

struct boundary_para
{
    std::string _boundary_str;
    uint32_t _boundary_content_length;
    boundary_para()
    {
        init();
    }

    void init()
    {
        _boundary_str.clear();
        _boundary_content_length = (uint32_t)-1;
    }
};

const uint32_t BOUNDARY_EXTRA_LEN = 8;

class CMyCommonException : public std::exception
{
    public:

        CMyCommonException(const std::string &err_str)
        {
            _errstr = err_str;
            LOG_WARNING("THROW_COMMON_EXCEPT:%s", err_str.c_str());
        }

        virtual ~CMyCommonException() throw(){};

    public:
        virtual const char* what() const throw()
        {
            return _errstr.c_str();
        }

    protected:
        std::string _errstr;
};

#define THROW_COMMON_EXCEPT(errorstr) \
do { \
    std::stringstream ss; \
    ss << errorstr; \
    throw CMyCommonException(ss.str());\
} while (0)

void get_proc_name(char buf[], size_t buf_len);

#define SPLIT_MODE_ONE 1
#define SPLIT_MODE_ALL (1 << 1)
#define SPLIT_MODE_TRIM (1 << 2)

int SplitString(const char *srcStr, const char *delim, std::vector<std::string> * strVec, int s_mode);
std::string StringTrim(std::string &sSrc);
int GetCaseStringByLabel(const std::string &sSrc,const std::string &sLabel1,const std::string &sLabel2, std::string &sOut, unsigned int nBeginPos = 0, int nIfRetPos = 0);

void set_unblock(int fd);
uint64_t GetMilliSecond();
std::string strError(int errnum);

std::string SecToHttpTime(time_t tmpTime);

struct TMonth
{
    int nMonth;
    char sMonth[10];
};

struct TWeek
{
    int nWeek;
    char sWeek[10];
};

const TWeek WEEKARRAY[] =
{
    {0, "Sun"},
    {1, "Mon"},
    {2, "Tue"},
    {3, "Wed"},
    {4, "Thu"},
    {5, "Fri"},
    {6, "Sat"}
};

const TMonth MONTHARRAY[]=
{
    {1, "Jan"},
    {2, "Feb"},
    {3, "Mar"},
    {4, "Apr"},
    {5, "May"},
    {6, "Jun"},
    {7, "Jul"},
    {8, "Aug"},
    {9, "Sep"},
    {10, "Oct"},
    {11, "Nov"},
    {12, "Dec"}
};

std::string GetMonStr(int nMonth);
std::string GetWeekStr(int nWeek);

class CRxIniConfig {
public:
    bool load(const std::string &path);

    std::string value(const std::string &section, const std::string &key,
                      const std::string &def = "") const;

    int intValue(const std::string &section, const std::string &key, int def = 0) const;

    double doubleValue(const std::string &section, const std::string &key, double def = 0.0) const;

    bool boolValue(const std::string &section, const std::string &key, bool def = false) const;

    std::vector<std::string> sections() const;

    std::vector<std::string> keys(const std::string &section) const;

private:
    static int tolower_func(int ch);
    static bool not_space(int ch);
    static void ltrim(std::string &s);
    static void rtrim(std::string &s);
    static void trim(std::string &s);

    std::map<std::string, std::map<std::string, std::string> > data_;
};

inline bool CRxIniConfig::load(const std::string &path)
{
    std::ifstream in(path.c_str());
    if (!in) {
        return false;
    }

    std::string line, currentSection;
    while (std::getline(in, line)) {
        trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (!line.empty() && line[0] == '[' && line[line.size()-1] == ']') {
            currentSection = line.substr(1, line.size() - 2);
            trim(currentSection);
        } else {
            std::size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }
            std::string key   = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            trim(key);
            trim(value);
            data_[currentSection][key] = value;
        }
    }
    return true;
}

inline std::string CRxIniConfig::value(const std::string &section, const std::string &key,
                                       const std::string &def) const
{
    std::map<std::string, std::map<std::string, std::string> >::const_iterator sit = data_.find(section);
    if (sit == data_.end()) {
        return def;
    }
    std::map<std::string, std::string>::const_iterator kit = sit->second.find(key);
    return kit == sit->second.end() ? def : kit->second;
}

inline int CRxIniConfig::intValue(const std::string &section, const std::string &key, int def) const
{
    std::string v = value(section, key);
    return v.empty() ? def : std::atoi(v.c_str());
}

inline double CRxIniConfig::doubleValue(const std::string &section, const std::string &key, double def) const
{
    std::string v = value(section, key);
    return v.empty() ? def : std::atof(v.c_str());
}

inline bool CRxIniConfig::boolValue(const std::string &section, const std::string &key, bool def) const
{
    std::string v = value(section, key);
    if (v.empty()) {
        return def;
    }
    std::string low = v;
    std::transform(low.begin(), low.end(), low.begin(), tolower_func);
    return (low == "1" || low == "true" || low == "yes" || low == "on");
}

inline int CRxIniConfig::tolower_func(int ch)
{
    return std::tolower(ch);
}

inline bool CRxIniConfig::not_space(int ch)
{
    return !std::isspace(ch);
}

inline void CRxIniConfig::ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
}

inline void CRxIniConfig::rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

inline void CRxIniConfig::trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

inline std::vector<std::string> CRxIniConfig::sections() const
{
    std::vector<std::string> names;
    for (std::map<std::string, std::map<std::string, std::string> >::const_iterator it = data_.begin();
         it != data_.end(); ++it) {
        names.push_back(it->first);
    }
    return names;
}

inline std::vector<std::string> CRxIniConfig::keys(const std::string &section) const
{
    std::vector<std::string> keyNames;
    std::map<std::string, std::map<std::string, std::string> >::const_iterator sit = data_.find(section);
    if (sit != data_.end()) {
        for (std::map<std::string, std::string>::const_iterator kit = sit->second.begin();
             kit != sit->second.end(); ++kit) {
            keyNames.push_back(kit->first);
        }
    }
    return keyNames;
}

#define RXTRACENETCAP_LEGACY_CORE_COMMON_INCLUDED
#include "legacy_core_net.h"

#endif

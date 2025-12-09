#include "legacy_core.h"

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : max_file_size_(100 * 1024 * 1024), log_level_(LOG_DEBUG), initialized_(false) {
    pthread_mutex_init(&mutex_, NULL);
    memset(log_files_, 0, sizeof(log_files_));
}

Logger::~Logger() {
    pthread_mutex_lock(&mutex_);
    for (int i = 0; i < 32; ++i) {
        if (log_files_[i]) {
            fclose(log_files_[i]);
            log_files_[i] = NULL;
        }
    }
    pthread_mutex_unlock(&mutex_);
    pthread_mutex_destroy(&mutex_);
}

namespace {

static int ParseLogLevelMask(const std::string& level_text, int fallback)
{
    if (level_text.empty()) {
        return fallback;
    }

    std::string normalized;
    normalized.reserve(level_text.size());
    for (size_t i = 0; i < level_text.size(); ++i) {
        char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(level_text[i])));
        if (ch == '|' || ch == '+') {
            ch = ',';
        }
        normalized.push_back(ch);
    }

    int mask = 0;
    std::stringstream ss(normalized);
    std::string token;
    while (std::getline(ss, token, ',')) {

        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        std::string item = token.substr(start, end - start + 1);
        if (item == "fatal") {
            mask |= LOG_FATAL;
        } else if (item == "warning" || item == "warn") {
            mask |= LOG_WARNING;
        } else if (item == "notice" || item == "info") {
            mask |= LOG_NOTICE;
        } else if (item == "trace") {
            mask |= LOG_TRACE;
        } else if (item == "debug") {
            mask |= LOG_DEBUG;
        } else {

            int value = atoi(item.c_str());
            if (value > 0) {
                mask |= value;
            }
        }
    }

    return mask ? mask : fallback;
}

}

void Logger::init(const char* log_path, const char* prefix, unsigned int max_file_size, int log_level) {
    pthread_mutex_lock(&mutex_);

    log_path_ = log_path ? log_path : "logs";

    if (prefix) {
        prefix_ = prefix;
    } else {
        char proc_name[256];
        memset(proc_name, 0, sizeof(proc_name));
        if (readlink("/proc/self/exe", proc_name, sizeof(proc_name) - 1) != -1) {
            prefix_ = basename(proc_name);
        } else {
            prefix_ = "app";
        }
    }

    max_file_size_ = max_file_size;
    log_level_ = log_level;

    createDirectory(log_path_);

    initialized_ = true;

    pthread_mutex_unlock(&mutex_);
}

void Logger::init_from_path(const char* config)
{
    const unsigned int kDefaultMaxSize = 100 * 1024 * 1024;
    const int kDefaultLevel = LOG_LEVEL_DEBUG;

    if (!config || !*config) {
        init("logs", NULL, kDefaultMaxSize, kDefaultLevel);
        return;
    }

    std::string input(config);
    std::string options;
    size_t qpos = input.find('?');
    if (qpos != std::string::npos) {
        options = input.substr(qpos + 1);
        input = input.substr(0, qpos);
    }

    bool treat_as_file = false;
    size_t slash = input.find_last_of('/');
    std::string directory;
    std::string filename;

    if (slash == std::string::npos) {
        directory = input;
        filename.clear();
    } else {
        directory = input.substr(0, slash);
        filename = input.substr(slash + 1);
    }

    if (!filename.empty()) {
        size_t dot = filename.find_last_of('.');
        if (dot != std::string::npos) {
            treat_as_file = true;
        }
    } else if (!directory.empty()) {

        if (directory[directory.size() - 1] == '/') {
            directory.erase(directory.size() - 1);
        }
    }

    std::string resolved_dir;
    std::string resolved_prefix;

    if (treat_as_file) {
        resolved_dir = directory.empty() ? "." : directory;
        size_t dot = filename.find_last_of('.');
        resolved_prefix = filename.substr(0, dot);
    } else {
        resolved_dir = input.empty() ? "logs" : input;
        resolved_prefix.clear();
    }

    unsigned int resolved_size = kDefaultMaxSize;
    int resolved_level = kDefaultLevel;

    if (!options.empty()) {
        std::stringstream opt_stream(options);
        std::string kv;
        while (std::getline(opt_stream, kv, '&')) {
            size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = kv.substr(0, eq);
            std::string value = kv.substr(eq + 1);

            size_t start = key.find_first_not_of(" \t\r\n");
            size_t end = key.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                continue;
            }
            key = key.substr(start, end - start + 1);
            start = value.find_first_not_of(" \t\r\n");
            end = value.find_last_not_of(" \t\r\n");
            value = start == std::string::npos ? std::string() : value.substr(start, end - start + 1);

            if (key == "prefix") {
                resolved_prefix = value;
            } else if (key == "size" || key == "size_mb") {
                long size_mb = atol(value.c_str());
                if (size_mb > 0) {
                    resolved_size = static_cast<unsigned int>(size_mb) * 1024u * 1024u;
                }
            } else if (key == "level") {
                resolved_level = ParseLogLevelMask(value, kDefaultLevel);
            }
        }
    }

    init(resolved_dir.c_str(),
         resolved_prefix.empty() ? NULL : resolved_prefix.c_str(),
         resolved_size,
         resolved_level);
}

void Logger::writeLog(LogLevel level, const char* file, int line, const char* func, const char* format, ...)
{
    if (!initialized_) {
        init();
    }

    if (!(log_level_ & level)) {
        return;
    }

    pthread_mutex_lock(&mutex_);

    checkRotateFile(level);

    const size_t kBufferSize = 64 * 1024;
    char buffer[kBufferSize];
    buffer[0] = '\0';

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, kBufferSize, format, args);
    va_end(args);

    std::stringstream ss;
    ss << getTimestamp();
    ss << getLevelName(level);
    ss << " ";
    ss << file << ":" << line << " [" << func << "] ";
    ss << buffer;
    ss << "\n";

    FILE* output = log_files_[static_cast<int>(level)];
    if (output) {
        fwrite(ss.str().c_str(), 1, ss.str().size(), output);
        fflush(output);
    }

    pthread_mutex_unlock(&mutex_);
}

void Logger::setLogLevel(int level)
{
    log_level_ = level;
}

const char* Logger::getLevelName(LogLevel level)
{
    switch (level) {
    case LOG_FATAL:
        return "[FATAL] ";
    case LOG_WARNING:
        return "[WARNING] ";
    case LOG_NOTICE:
        return "[NOTICE] ";
    case LOG_TRACE:
        return "[TRACE] ";
    case LOG_DEBUG:
        return "[DEBUG] ";
    default:
        return "[UNKNOWN]";
    }
}

void Logger::checkRotateFile(LogLevel level)
{
    FILE* file = log_files_[static_cast<int>(level)];
    if (file) {
        struct stat st;
        if (fstat(fileno(file), &st) == 0) {
            if (static_cast<unsigned long>(st.st_size) < max_file_size_) {
                return;
            }
        }
    }

    if (file) {
        fclose(file);
        log_files_[static_cast<int>(level)] = NULL;
    }

    std::string path = getLogFilePath(level);
    file = fopen(path.c_str(), "a");
    if (!file) {
        fprintf(stderr, "Failed to open log file %s\n", path.c_str());
        return;
    }

    log_files_[static_cast<int>(level)] = file;
}

std::string Logger::getLogFilePath(LogLevel level)
{
    std::string filename = prefix_;
    filename += "_";
    filename += getLevelName(level);
    filename += ".log";
    return log_path_ + "/" + filename;
}

std::string Logger::getTimestamp()
{
    char buf[64];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    return std::string(buf);
}

bool Logger::createDirectory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    return false;
}

LogStream::LogStream(LogLevel level, int line, const char* func, const char* file)
    : level_(level)
{
    ss_ << "[" << file << ":" << line << " " << func << "] ";
}

LogStream::~LogStream()
{
    Logger::getInstance().writeLog(level_, "", 0, "", "%s", ss_.str().c_str());
}

std::string LogStream::str() const
{
    return ss_.str();
}

CRxThreadMutex::CRxThreadMutex()
{
    pthread_mutex_init(&_mutex, NULL);
}

CRxThreadMutex::~CRxThreadMutex()
{
    pthread_mutex_destroy(&_mutex);
}

void CRxThreadMutex::lock()
{
    pthread_mutex_lock(&_mutex);
}

void CRxThreadMutex::unlock()
{
    pthread_mutex_unlock(&_mutex);
}

CRxThreadLock::CRxThreadLock(CRxThreadMutex * mutex)
{
    _mutex = mutex;
    if (_mutex != NULL){
        _mutex->lock();
    }
}

CRxThreadLock::~CRxThreadLock()
{
    if (_mutex !=NULL){
        _mutex->unlock();
    }
}

CRxThreadRwlock::CRxThreadRwlock()
{
    pthread_rwlock_init(&_rwlock, NULL);
}

CRxThreadRwlock::~CRxThreadRwlock()
{
    pthread_rwlock_destroy(&_rwlock);
}

void CRxThreadRwlock::read_lock()
{
    pthread_rwlock_rdlock(&_rwlock);
}

void CRxThreadRwlock::write_lock()
{
    pthread_rwlock_wrlock(&_rwlock);
}

void CRxThreadRwlock::unlock()
{
    pthread_rwlock_unlock(&_rwlock);
}

std::string strError(int errnum)
{
    char err_buf[SIZE_LEN_1024];
    err_buf[0] = '\0';
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    char* msg = strerror_r(errnum, err_buf, sizeof(err_buf));
    if (!msg) {
        snprintf(err_buf, sizeof(err_buf), "Unknown error %d", errnum);
        return std::string(err_buf);
    }
    return std::string(msg);
#else
    int rc = strerror_r(errnum, err_buf, sizeof(err_buf));
    if (rc != 0) {
        snprintf(err_buf, sizeof(err_buf), "Unknown error %d", errnum);
    }
    return std::string(err_buf);
#endif
}

uint64_t GetMilliSecond()
{
    timeval tm;
    gettimeofday(&tm, NULL);
    uint64_t now = tm.tv_sec * 1000 + tm.tv_usec/1000;
    return now;
}

void get_proc_name(char buf[], size_t buf_len) {

    if (!buf || !buf_len) {
        return;
    }

    buf[0] = '\0';
    pid_t pid = getpid();
    char exec_file[SIZE_LEN_1024];
    char proc_name[SIZE_LEN_1024];
    snprintf(exec_file, sizeof(exec_file), "/proc/%d/exe", pid);
    int ret = readlink(exec_file, proc_name, sizeof(proc_name));
    if (-1 == ret)
    {
        return;
    }
    proc_name[ret] = 0;

    char *p = strrchr(proc_name, '/');
    if (NULL == p)
    {
        return;
    }
    snprintf(buf, buf_len, "%s", p + 1);
    return;
}

void set_unblock(int fd)
{
    int ret = fcntl(fd, F_GETFL);

    fcntl(fd, F_SETFL, ret | O_NONBLOCK);
}

int GetCaseStringByLabel(const std::string &sSrc,const std::string &sLabel1,const std::string &sLabel2, std::string &sOut, unsigned int nBeginPos, int nIfRetPos)
{
    if (nBeginPos >= sSrc.length())
    {
        return -1;
    }
    char *pTmp = (char*)sSrc.c_str() + nBeginPos;
    char *pTmp0;
    char *pTmp1;
    if (sLabel1 == "")
    {
        pTmp0 = pTmp;
    }
    else
    {
        pTmp0= strcasestr(pTmp, sLabel1.c_str());
        if (pTmp0 == NULL)
        {
            return -1;
        }
    }
    int ret=pTmp0-sSrc.c_str()+sLabel1.length();
    if (sLabel2 != "")
    {
        pTmp1 = strcasestr(pTmp0+sLabel1.length(), sLabel2.c_str());if (pTmp1 == NULL)
        {
            return -1;
        }
        ret=pTmp1+sLabel2.length()-sSrc.c_str();
        sOut=std::string(pTmp0+sLabel1.length(), pTmp1-pTmp0-sLabel1.length());
    }
    else
    {
        sOut = std::string(pTmp0+sLabel1.length());
        ret=sSrc.length();
    }
    if (nIfRetPos == 0)
    {
        ret = 0;
    }
    return ret;
}

std::string  StringTrim(std::string &sSrc)
{
    int i = 0;
    while ((sSrc[i] == ' ' || sSrc[i] == '\t' || sSrc[i] == '\r' || sSrc[i] == '\n') && i < (int)sSrc.length())
    {
        i++;
    }
    int nBeginPos = i;
    i = sSrc.length() - 1;
    while ((sSrc[i] == ' ' || sSrc[i] == '\t' || sSrc[i] == '\r' || sSrc[i] == '\n') && i >=0)
    {
        i--;
    }
    int nEnd = i;
    sSrc = sSrc.substr(nBeginPos, nEnd - nBeginPos + 1);

    return sSrc;
}

int SplitString(const char *srcStr, const char *delim, std::vector<std::string> * strVec, int s_mode)
{
    if (!srcStr || !delim || !strVec)
    {
        return 0;
    }

    strVec->clear();

    char *pstart = const_cast<char *>(srcStr);
    char * pend = NULL;
    int count = 0;

    size_t dlen = strlen(delim);
    while (1)
    {
        pend = strstr(pstart, delim);
        if (pend)
        {
            std::string str(pstart, pend);
            pstart =  pend + dlen;

            if (s_mode & SPLIT_MODE_TRIM)
            {
                if (!strncmp(str.c_str(), delim, dlen))
                    continue;
            }

            strVec->push_back(str);
            count += 1;
            if (SPLIT_MODE_ONE & s_mode)
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

    if (*pstart != '\0' && pstart != srcStr)
    {
        std::string str(pstart);
        strVec->push_back(str);
        count += 1;
    }

    return count;
}

std::string GetMonStr(int nMonth)
{
    std::string sMonth;
    if (nMonth >=1 && nMonth <=12)
    {
        sMonth = std::string(MONTHARRAY[nMonth-1].sMonth);
    }
    return sMonth;
}

std::string GetWeekStr(int nWeek)
{
    std::string sWeek;
    if (nWeek >=0 && nWeek <=6)
    {
        sWeek = std::string(WEEKARRAY[nWeek].sWeek);
    }
    return sWeek;
}

std::string SecToHttpTime(time_t tmpTime)
{
    struct timeval tm1;
    struct timezone tz1;
    gettimeofday(&tm1, &tz1);
    tmpTime = tmpTime + tz1.tz_minuteswest*60;
    tm tmpTm;
    localtime_r(&tmpTime, &tmpTm);
    char sTime[128] = {0};

    snprintf(sTime, sizeof(sTime),"%s, %d %s %d %02d:%02d:%02d GMT ", GetWeekStr(tmpTm.tm_wday).c_str(),  tmpTm.tm_mday,
            GetMonStr(tmpTm.tm_mon + 1).c_str(),
            tmpTm.tm_year+1900, tmpTm.tm_hour, tmpTm.tm_min,   tmpTm.tm_sec);
    return std::string(sTime);
}

bool operator<(const ObjId & oj1, const ObjId & oj2)
{
    if (oj1._id != oj2._id){
        return oj1._id < oj2._id;
    } else if (oj1._thread_index != oj2._thread_index) {
        return oj1._thread_index < oj2._thread_index;
    }else
        return false;
}

bool operator==(const ObjId & oj1, const ObjId & oj2)
{
    return oj1._thread_index ==  oj2._thread_index && oj1._id == oj2._id;
}

http_response_code http_response_code::response;

std::string http_response_code::get_response_str(int status_code)
{
    std::string str;
    std::map<int, std::string>::iterator tmp_itr = _response_list.find(status_code);
    if (tmp_itr == _response_list.end())
    {
        LOG_DEBUG("http response code not found:%d", status_code);
        return str;
    }

    return tmp_itr->second;
}

void http_req_head_para::to_head_str(std::string * head)
{
    if (!head)
    {
        return;
    }

    head->append(_method);
    head->append(" ");
    head->append(_url_path);
    head->append(" ");
    head->append(_version);
    head->append(CRLF);

    if (_cookie_list.size() > 0)
    {
        head->append("Cookie");
        head->append(": ");
        int ii = 0;
        for (std::map<std::string, std::string>::iterator itr = _cookie_list.begin();
                itr != _cookie_list.end(); ++itr)
        {
            if (ii > 0)
                head->append(";");
            head->append(itr->first);
            head->append("=");
            head->append(itr->second);
            ii++;
        }
        head->append(CRLF);
    }

    for (std::map<std::string, std::string>::iterator itr = _headers.begin();itr != _headers.end(); ++itr)
    {
        head->append(itr->first);
        head->append(": ");
        head->append(itr->second);
        head->append(CRLF);
    }

    head->append(CRLF);

}

void http_res_head_para::to_head_str(std::string * head)
{
    if (!head) {
        return;
    }

    if (!_response_str.empty())
        _response_str = http_response_code::response.get_response_str(_response_code);

    {
        head->append(_version);
        head->append(" ");
        char tmp_buf[SIZE_LEN_32];
        snprintf(tmp_buf, sizeof(tmp_buf), "%d", _response_code);
        head->append(tmp_buf);
        head->append(" ");
        head->append(_response_str);
        head->append(CRLF);
    }

    if (_cookie_list.size() > 0)
    {
        for (std::map<std::string, set_cookie_item>::iterator itr = _cookie_list.begin();
                itr != _cookie_list.end(); ++itr)
        {
            head->append("Set-Cookie: ");
            head->append(itr->first);
            head->append("=");
            head->append(itr->second._value);
            if (itr->second._expire != 0)
            {
                head->append(";expires=");
                head->append(SecToHttpTime(itr->second._expire));
            }

            if (itr->second._path != "")
            {
                head->append(";path=");
                head->append(itr->second._path);
            }

            if (itr->second._domain != "")
            {
                head->append(";domain=");
                head->append(itr->second._domain);
            }
            head->append(CRLF);
        }
    }

    for (std::map<std::string, std::string>::iterator itr = _headers.begin();
            itr != _headers.end(); ++itr)
    {
        head->append(itr->first);
        head->append(": ");
        head->append(itr->second);
        head->append(CRLF);
    }

    head->append(CRLF);
}

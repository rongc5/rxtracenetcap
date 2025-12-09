#include "rxcapturemanagerthread.h"
#include "rxcapturethread.h"
#include "legacy_core.h"
#include "rxprocdata.h"
#include "rxcapturemessages.h"
#include "rxprocessresolver.h"
#include "rxcleanupthread.h"
#include "rxsamplethread.h"
#include <cstdio>
#include <malloc.h>
#include <time.h>
#include <sstream>
#include <set>
#include <map>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <algorithm>

namespace {

const int kFallbackDurationSec = 60;

const char* capture_status_to_string(ECaptureTaskStatus status)
{
    switch (status) {
        case STATUS_PENDING: return "pending";
        case STATUS_RESOLVING: return "resolving";
        case STATUS_RUNNING: return "running";
        case STATUS_COMPLETED: return "completed";
        case STATUS_FAILED: return "failed";
        case STATUS_STOPPED: return "stopped";
        default: return "unknown";
    }
}

const char* capture_mode_to_string(ECaptureMode mode)
{
    switch (mode) {
        case MODE_INTERFACE: return "interface";
        case MODE_PROCESS: return "process";
        case MODE_PID: return "pid";
        case MODE_CONTAINER: return "container";
        default: return "unknown";
    }
}

std::string json_escape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

static std::string trim_copy(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

static std::string to_lower_copy(const std::string& value)
{
    std::string out = value;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

static std::string strip_quotes_copy(const std::string& value)
{
    if (value.size() >= 2) {
        char first = value[0];
        char last = value[value.size() - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static std::map<std::string, std::string> parse_capture_hint_pairs(const std::string& hint)
{
    std::map<std::string, std::string> result;
    std::string normalized = hint;
    for (size_t i = 0; i < normalized.size(); ++i) {
        char& ch = normalized[i];
        if (ch == ',' || ch == ';') {
            ch = ' ';
        }
    }
    std::istringstream ss(normalized);
    std::string token;
    while (ss >> token) {
        size_t pos = token.find_first_of(":=");
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = to_lower_copy(trim_copy(token.substr(0, pos)));
        std::string value = trim_copy(token.substr(pos + 1));
        value = strip_quotes_copy(value);
        result[key] = value;
    }
    return result;
}

static void append_json_str_field(std::ostringstream& oss, bool& first, const char* key, const std::string& value)
{
    if (value.empty()) {
        return;
    }
    if (!first) {
        oss << ',';
    } else {
        first = false;
    }
    oss << '"' << key << '"' << ':' << '"' << json_escape(value) << '"';
}

static void append_json_int_field(std::ostringstream& oss, bool& first, const char* key, long long value, bool include_zero = false)
{
    if (!include_zero && value == 0) {
        return;
    }
    if (!first) {
        oss << ',';
    } else {
        first = false;
    }
    oss << '"' << key << '"' << ':' << value;
}

static void append_json_bool_field(std::ostringstream& oss, bool& first, const char* key, bool value, bool include_false = true)
{
    if (!value && !include_false) {
        return;
    }
    if (!first) {
        oss << ',';
    } else {
        first = false;
    }
    oss << '"' << key << '"' << ':' << (value ? "true" : "false");
}

static void append_json_int_array_field(std::ostringstream& oss, bool& first, const char* key, const std::vector<long long>& values)
{
    if (values.empty()) {
        return;
    }
    if (!first) {
        oss << ',';
    } else {
        first = false;
    }
    oss << '"' << key << '"' << ':';
    oss << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << values[i];
    }
    oss << ']';
}

static std::string normalize_filter_for_signature(const std::string& filter)
{
    std::string normalized = filter;
    StringTrim(normalized);
    return normalized;
}

static std::string build_capture_signature(const CaptureSpec& spec,
                                           const CaptureConfigSnapshot& config_snapshot,
                                           const std::vector<SProcessInfo>& matched_processes)
{
    std::ostringstream oss;
    oss << '{';
    bool first = true;

    append_json_str_field(oss, first, "mode", capture_mode_to_string(spec.capture_mode));
    append_json_str_field(oss, first, "iface", spec.iface);
    append_json_str_field(oss, first, "resolved_iface", spec.resolved_iface);
    append_json_str_field(oss, first, "category", spec.category);
    append_json_str_field(oss, first, "output_pattern",
                          spec.output_pattern.empty() ? config_snapshot.filename_template : spec.output_pattern);
    append_json_str_field(oss, first, "output_dir", config_snapshot.output_dir);

    std::string normalized_filter = normalize_filter_for_signature(spec.filter);
    append_json_str_field(oss, first, "filter", normalized_filter);
    append_json_str_field(oss, first, "protocol_filter", spec.protocol_filter);
    append_json_str_field(oss, first, "ip_filter", spec.ip_filter);
    append_json_int_field(oss, first, "port_filter", spec.port_filter);

    int effective_duration = spec.max_duration_sec > 0 ? spec.max_duration_sec : config_snapshot.max_duration_sec;
    if (effective_duration > 0) {
        append_json_int_field(oss, first, "max_duration_sec", effective_duration);
    }

    long effective_max_bytes = spec.max_bytes > 0 ? spec.max_bytes : config_snapshot.max_bytes;
    if (effective_max_bytes > 0) {
        append_json_int_field(oss, first, "max_bytes", effective_max_bytes);
    }

    int effective_max_packets = spec.max_packets > 0 ? spec.max_packets : config_snapshot.max_packets;
    if (effective_max_packets > 0) {
        append_json_int_field(oss, first, "max_packets", effective_max_packets);
    }

    append_json_int_field(oss, first, "snaplen", spec.snaplen, true);
    append_json_bool_field(oss, first, "compress_enabled", config_snapshot.compress_enabled);
    append_json_int_field(oss, first, "compress_threshold_mb", config_snapshot.compress_threshold_mb, true);
    append_json_str_field(oss, first, "compress_format", config_snapshot.compress_format);
    append_json_bool_field(oss, first, "compress_remove_src", config_snapshot.compress_remove_src);

    append_json_str_field(oss, first, "netns_path", spec.netns_path);

    if (spec.capture_mode == MODE_PROCESS) {
        append_json_str_field(oss, first, "proc_name", spec.proc_name);
        std::vector<long long> pids;
        pids.reserve(matched_processes.size());
        for (size_t i = 0; i < matched_processes.size(); ++i) {
            if (matched_processes[i].pid > 0) {
                pids.push_back(static_cast<long long>(matched_processes[i].pid));
            }
        }
        std::sort(pids.begin(), pids.end());
        pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
        append_json_int_array_field(oss, first, "matched_pids", pids);
    } else if (spec.capture_mode == MODE_PID) {
        if (spec.target_pid > 0) {
            append_json_int_field(oss, first, "target_pid", static_cast<long long>(spec.target_pid), true);
        }
    } else if (spec.capture_mode == MODE_CONTAINER) {
        append_json_str_field(oss, first, "container_id", spec.container_id);
    }

    oss << '}';
    return oss.str();
}

static std::string make_sid_with_timestamp(const std::string& signature)
{
    int64_t now_usec = rx_capture_now_usec();
    time_t secs = static_cast<time_t>(now_usec / 1000000LL);
#if defined(_WIN32)
    struct tm tm_now;
    localtime_s(&tm_now, &secs);
#else
    struct tm tm_now;
    localtime_r(&secs, &tm_now);
#endif
    int millis = static_cast<int>((now_usec / 1000LL) % 1000LL);
    std::ostringstream ts;
    ts << std::setfill('0')
       << std::setw(4) << (tm_now.tm_year + 1900)
       << std::setw(2) << (tm_now.tm_mon + 1)
       << std::setw(2) << tm_now.tm_mday
       << std::setw(2) << tm_now.tm_hour
       << std::setw(2) << tm_now.tm_min
       << std::setw(2) << tm_now.tm_sec
       << std::setw(3) << millis;
    std::string sid = signature;
    sid += ts.str();
    return sid;
}

static int infer_port_from_filter(const std::string& filter)
{
    if (filter.empty()) {
        return 0;
    }

    const size_t len = filter.size();
    size_t pos = 0;
    int found_port = 0;
    while ((pos = filter.find("port", pos)) != std::string::npos) {
        if (pos > 0) {
            unsigned char prev = static_cast<unsigned char>(filter[pos - 1]);
            if (std::isalnum(prev) || prev == '_') {
                pos += 4;
                continue;
            }
        }

        size_t idx = pos + 4;
        while (idx < len && std::isspace(static_cast<unsigned char>(filter[idx]))) {
            ++idx;
        }
        if (idx < len && (filter[idx] == '=' || filter[idx] == ':')) {
            ++idx;
            while (idx < len && std::isspace(static_cast<unsigned char>(filter[idx]))) {
                ++idx;
            }
        }
        size_t start = idx;
        if (start < len && (filter[start] == '+' || filter[start] == '-')) {
            ++idx;
            start = idx;
        }
        while (idx < len && std::isdigit(static_cast<unsigned char>(filter[idx]))) {
            ++idx;
        }
        if (idx > start) {
            std::string num = filter.substr(start, idx - start);
            int port = std::atoi(num.c_str());
            if (port > 0 && port <= 65535) {
                if (found_port == 0) {
                    found_port = port;
                } else if (port != found_port) {
                    /* Multiple distinct ports present; do not collapse to one */
                    return 0;
                }
            }
        }
        pos += 4;
    }
    return found_port;
}

static bool parse_int_value(const std::string& text, int& out)
{
    char* endptr = NULL;
    long value = std::strtol(text.c_str(), &endptr, 10);
    if (endptr == text.c_str() || (endptr && *endptr != '\0')) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

static bool parse_long_value(const std::string& text, long& out)
{
    char* endptr = NULL;
    long value = std::strtol(text.c_str(), &endptr, 10);
    if (endptr == text.c_str() || (endptr && *endptr != '\0')) {
        return false;
    }
    out = value;
    return true;
}

struct AssignWorkerFunctor {
    uint32_t worker_id;
    explicit AssignWorkerFunctor(uint32_t wid) : worker_id(wid) {}
    void operator()(SRxCaptureTask& task) const {
        task.worker_thread_index = worker_id;
        task.stop_requested = false;
        task.cancel_requested = false;
        if (task.status == STATUS_PENDING) {
            task.status = STATUS_RESOLVING;
        }
    }
};

struct MarkStopFunctor {
    bool flag;
    explicit MarkStopFunctor(bool f) : flag(f) {}
    void operator()(SRxCaptureTask& task) const {
        task.stop_requested = flag;
        if (flag) {
            task.cancel_requested = false;
        }
    }
};

struct MarkStoppedFunctor {
    int64_t ts_usec;
    unsigned long packets;
    unsigned long bytes;
    std::string message;
    MarkStoppedFunctor(int64_t ts, unsigned long p, unsigned long b, const std::string& msg)
        : ts_usec(ts), packets(p), bytes(b), message(msg) {}
    void operator()(SRxCaptureTask& task) const {
        long secs = (ts_usec <= 0) ? static_cast<long>(::time(NULL))
                                   : static_cast<long>(ts_usec / 1000000LL);
        task.end_time = secs;
        task.packet_count = packets;
        task.bytes_captured = bytes;
        task.error_message = message;
        task.stop_requested = false;
    }
};

}
SRxCaptureTask::SRxCaptureTask()
    : capture_id(-1)
    , capture_mode(MODE_INTERFACE)
    , target_pid(-1)
    , port_filter(0)
    , duration_sec(0)
    , max_bytes(0)
    , max_packets(0)
    , priority(0)
    , status(STATUS_PENDING)
    , capture_pid(-1)
    , start_time(0)
    , end_time(0)
    , packet_count(0)
    , bytes_captured(0)
{
    worker_thread_index = 0;
    stop_requested = false;
    cancel_requested = false;
}

CRxCaptureManagerThread::CRxCaptureManagerThread()
    : _is_first(false)
{
}

CRxCaptureManagerThread::~CRxCaptureManagerThread()
{

    for (size_t i = 0; i < _capture_threads.size(); ++i)
    {
        if (_capture_threads[i])
        {
            delete _capture_threads[i];
            _capture_threads[i] = NULL;
        }
    }
    _capture_threads.clear();
}

bool CRxCaptureManagerThread::start()
{

    if (!base_net_thread::start())
    {
        LOG_ERROR("CRxCaptureManagerThread: base_net_thread::start() failed");
        return false;
    }

    LOG_NOTICE("CRxCaptureManagerThread started");

    CRxProcData* p_data = CRxProcData::instance();
    CRxServerConfig* cfg = p_data ? p_data->server_config() : NULL;
    if (!p_data || !cfg)
    {
        LOG_ERROR("CRxCaptureManagerThread: proc_data or config is NULL");
        return false;
    }

    int capture_threads_count = cfg->capture_threads();
    if (capture_threads_count <= 0)
    {
        LOG_WARNING("CRxCaptureManagerThread: capture_threads = %d, using default 2",
                    capture_threads_count);
        capture_threads_count = 2;
    }

    LOG_NOTICE("CRxCaptureManagerThread: creating %d capture worker threads",
               capture_threads_count);

    for (int i = 0; i < capture_threads_count; ++i)
    {
        CRxCaptureThread* worker = new (std::nothrow) CRxCaptureThread();
        if (!worker)
        {
            LOG_ERROR("CRxCaptureManagerThread: failed to allocate CRxCaptureThread %d", i);
            continue;
        }

        if (!worker->start())
        {
            LOG_ERROR("CRxCaptureManagerThread: failed to start CRxCaptureThread %d", i);
            delete worker;
            continue;
        }

        _capture_threads.push_back(worker);
        uint32_t thread_idx = worker->get_thread_index();
        add_worker_thread(thread_idx);

        LOG_NOTICE("CRxCaptureManagerThread: created capture worker thread %d, thread_index=%u",
                   i, thread_idx);
    }

    LOG_NOTICE("CRxCaptureManagerThread: created %zu capture worker threads",
               _capture_threads.size());

    return true;
}

void CRxCaptureManagerThread::add_worker_thread(uint32_t thread_index)
{
    _worker_thd_vec.push_back(thread_index);
    LOG_NOTICE("CRxCaptureManagerThread: added worker thread ID = %u", thread_index);
}

void CRxCaptureManagerThread::run_process()
{
    if (!_is_first)
    {
        _is_first = true;

        LOG_NOTICE("CRxCaptureManagerThread starting timers");

        start_queue_timer();
        start_clean_timer();
        start_compress_timer();
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (global_data) {
        CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
        task_mgr.cleanup_pending_deletes();
    }
}

void CRxCaptureManagerThread::handle_msg(shared_ptr<normal_msg>& msg)
{
    if (!msg)
        return;

    switch(msg->_msg_op)
    {
        case RX_MSG_START_CAPTURE:
            handle_start_capture(msg);
            break;
        case RX_MSG_STOP_CAPTURE:
            handle_stop_capture(msg);
            break;
        case RX_MSG_QUERY_CAPTURE:
            handle_query_capture(msg);
            break;
        case RX_MSG_TASK_UPDATE:
            handle_task_update(msg);
            break;
        case RX_MSG_CAPTURE_STARTED:
            handle_capture_started_v2(msg);
            break;
        case RX_MSG_CAPTURE_PROGRESS:
            handle_capture_progress_v2(msg);
            break;
        case RX_MSG_CAPTURE_FILE_READY:
            handle_capture_file_ready_v2(msg);
            break;
        case RX_MSG_CAPTURE_FINISHED:
            handle_capture_finished_v2(msg);
            break;
        case RX_MSG_CAPTURE_FAILED:
            handle_capture_failed_v2(msg);
            break;
        case RX_MSG_SAMPLE_TRIGGER:
        case RX_MSG_SAMPLE_ALERT:
            handle_sample_alert(msg);
            break;
        case RX_MSG_CLEAN_EXPIRED:
            handle_clean_expired(msg);
            break;
        case RX_MSG_COMPRESS_FILES:
            handle_compress_files(msg);
            break;
        case RX_MSG_CHECK_THRESHOLD:
            handle_check_threshold(msg);
            break;
        case RX_MSG_CLEAN_COMPRESS_DONE:
            handle_clean_compress_done(msg);
            break;
        case RX_MSG_CLEAN_COMPRESS_FAILED:
            handle_clean_compress_failed(msg);
            break;
        default:
            LOG_DEBUG("Unknown message operation: %d", msg->_msg_op);
            break;
    }
}

void CRxCaptureManagerThread::handle_sample_alert(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxSampleMsg> alert =
        dynamic_pointer_cast<SRxSampleMsg>(msg);
    if (!alert) {
        LOG_WARNING("handle_sample_alert: invalid message type");
        return;
    }

    if (alert->module_name.empty()) {
        alert->module_name = "default";
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    SRxSampleAlertRecord record;
    record.timestamp = alert->stats.timestamp;
    record.cpu_percent = alert->stats.cpu_percent;
    record.memory_percent = alert->stats.memory_percent;
    record.network_rx_kbps = alert->stats.network_rx_kbps;
    record.network_tx_kbps = alert->stats.network_tx_kbps;
    record.cpu_hit = alert->cpu_hit;
    record.mem_hit = alert->mem_hit;
    record.net_hit = alert->net_hit;
    record.module_name = alert->module_name;
    record.capture_hint = alert->capture_hint;
    record.capture_category = alert->capture_category;
    record.capture_duration_sec = alert->capture_duration_sec;
    record.cooldown_sec = alert->cooldown_sec;
    record.cpu_threshold = alert->cpu_threshold;
    record.mem_threshold = alert->mem_threshold;
    record.net_threshold = alert->net_threshold;
    uint64_t alert_id = global_data->record_sample_alert(record);
    alert->alert_id = alert_id;

    std::vector<base_net_thread*>* http_threads = global_data->get_thread("http_res");
    if (http_threads) {
        for (std::vector<base_net_thread*>::iterator it = http_threads->begin();
             it != http_threads->end(); ++it) {
            base_net_thread* thread = *it;
            if (!thread) {
                continue;
            }
            shared_ptr<SRxSampleMsg> notify(new SRxSampleMsg(*alert));
            notify->_msg_op = RX_MSG_SAMPLE_TRIGGER;
            shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(notify);
            ObjId target;
            target._id = OBJ_ID_THREAD;
            target._thread_index = thread->get_thread_index();
            base_net_thread::put_obj_msg(target, base);
        }
    }

    LOG_NOTICE("Sample alert handled id=%llu module=%s CPU=%d MEM=%d NET=%d",
               static_cast<unsigned long long>(alert_id),
               alert->module_name.c_str(),
               alert->cpu_hit ? 1 : 0,
               alert->mem_hit ? 1 : 0,
               alert->net_hit ? 1 : 0);

    if (!alert->capture_hint.empty()) {
        time_t now = time(NULL);
        int cooldown_sec = alert->cooldown_sec;
        if (cooldown_sec <= 0 && global_data->_strategy_dict) {
            CRxStrategyConfigManager* cfg = global_data->_strategy_dict->current();
            if (cfg) {
                cooldown_sec = cfg->limits().per_key_cooldown_sec;
            }
        }
        if (cooldown_sec <= 0) {
            cooldown_sec = 60;
        }

        if (should_throttle_module(alert->module_name, cooldown_sec, now)) {
            LOG_NOTICE("Sample module '%s' suppressed by cooldown (%d sec)",
                       alert->module_name.c_str(), cooldown_sec);
            return;
        }

        shared_ptr<SRxStartCaptureMsg> start_msg;
        if (!prepare_capture_from_sample(alert, start_msg)) {
            LOG_WARNING("Sample module '%s' capture_hint invalid: %s",
                        alert->module_name.c_str(),
                        alert->capture_hint.c_str());
            return;
        }

        _module_last_trigger[alert->module_name] = now;
        shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(start_msg);
        handle_start_capture(base);
        LOG_NOTICE("Auto capture triggered by module '%s'",
                   alert->module_name.c_str());
    }
}

bool CRxCaptureManagerThread::should_throttle_module(const std::string& module_name,
                                                     int cooldown_sec,
                                                     time_t now)
{
    if (cooldown_sec <= 0) {
        return false;
    }
    std::map<std::string, time_t>::const_iterator it = _module_last_trigger.find(module_name);
    if (it != _module_last_trigger.end()) {
        time_t last = it->second;
        if (now >= last && (now - last) < cooldown_sec) {
            return true;
        }
    }
    return false;
}

void CRxCaptureManagerThread::clear_module_cooldown_for_capture(int capture_id)
{
    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    TaskSnapshot snapshot;
    if (!task_mgr.query_task(capture_id, snapshot)) {
        return;
    }

    static const char kModulePrefix[] = "module:";
    const size_t prefix_len = sizeof(kModulePrefix) - 1;

    if (snapshot.request_user.size() <= prefix_len) {
        return;
    }
    if (snapshot.request_user.compare(0, prefix_len, kModulePrefix) != 0) {
        return;
    }

    std::string module_name = snapshot.request_user.substr(prefix_len);
    if (module_name.empty()) {
        return;
    }

    std::map<std::string, time_t>::iterator it = _module_last_trigger.find(module_name);
    if (it != _module_last_trigger.end()) {
        _module_last_trigger.erase(it);
        LOG_DEBUG("Cleared cooldown for module '%s' after capture %d",
                  module_name.c_str(), capture_id);
    }
}

bool CRxCaptureManagerThread::prepare_capture_from_sample(const shared_ptr<SRxSampleMsg>& alert,
                                                          shared_ptr<SRxStartCaptureMsg>& start_msg)
{
    if (!alert) {
        return false;
    }

    std::string module_name = alert->module_name.empty() ? "default" : alert->module_name;

    CRxProcData* global_data = CRxProcData::instance();
    CRxStrategyConfigManager* cfg = NULL;
    if (global_data && global_data->_strategy_dict) {
        cfg = global_data->_strategy_dict->current();
    }
    const SRxDefaults* defaults = NULL;
    if (cfg) {
        defaults = &cfg->defaults();
    }

    std::map<std::string, std::string> pairs = parse_capture_hint_pairs(alert->capture_hint);

    int capture_mode = MODE_INTERFACE;
    std::string target_value;
    bool recognized = false;

    if (pairs.count("process")) {
        capture_mode = MODE_PROCESS;
        target_value = pairs["process"];
        recognized = true;
    } else if (pairs.count("proc")) {
        capture_mode = MODE_PROCESS;
        target_value = pairs["proc"];
        recognized = true;
    } else if (pairs.count("pid")) {
        capture_mode = MODE_PID;
        target_value = pairs["pid"];
        recognized = true;
    } else if (pairs.count("container")) {
        capture_mode = MODE_CONTAINER;
        target_value = pairs["container"];
        recognized = true;
    } else if (pairs.count("container_id")) {
        capture_mode = MODE_CONTAINER;
        target_value = pairs["container_id"];
        recognized = true;
    } else if (pairs.count("iface")) {
        capture_mode = MODE_INTERFACE;
        target_value = pairs["iface"];
        recognized = true;
    } else if (pairs.count("interface")) {
        capture_mode = MODE_INTERFACE;
        target_value = pairs["interface"];
        recognized = true;
    } else {
        size_t pos = alert->capture_hint.find(':');
        if (pos != std::string::npos) {
            std::string key = to_lower_copy(trim_copy(alert->capture_hint.substr(0, pos)));
            std::string value = strip_quotes_copy(trim_copy(alert->capture_hint.substr(pos + 1)));
            if (key == "process" || key == "proc") {
                capture_mode = MODE_PROCESS;
                target_value = value;
                recognized = true;
            } else if (key == "pid") {
                capture_mode = MODE_PID;
                target_value = value;
                recognized = true;
            } else if (key == "container" || key == "container_id") {
                capture_mode = MODE_CONTAINER;
                target_value = value;
                recognized = true;
            } else if (key == "iface" || key == "interface") {
                capture_mode = MODE_INTERFACE;
                target_value = value;
                recognized = true;
            }
        }
    }

    if (!recognized) {
        return false;
    }

    if (target_value.empty() && defaults && capture_mode == MODE_INTERFACE) {
        target_value = defaults->iface;
    }

    shared_ptr<SRxStartCaptureMsg> msg(new SRxStartCaptureMsg());
    msg->capture_mode = capture_mode;

    switch (capture_mode) {
        case MODE_INTERFACE:
            msg->iface = target_value;
            if (msg->iface.empty()) {
                LOG_WARNING("Sample module '%s' missing interface in capture hint '%s'",
                            module_name.c_str(), alert->capture_hint.c_str());
                return false;
            }
            break;
        case MODE_PROCESS:
            msg->proc_name = target_value;
            if (msg->proc_name.empty()) {
                LOG_WARNING("Sample module '%s' missing process name in capture hint '%s'",
                            module_name.c_str(), alert->capture_hint.c_str());
                return false;
            }
            break;
        case MODE_PID: {
            int pid = 0;
            if (!parse_int_value(target_value, pid) || pid <= 0) {
                LOG_WARNING("Sample module '%s' invalid pid '%s'",
                            module_name.c_str(), target_value.c_str());
                return false;
            }
            msg->target_pid = pid;
            break;
        }
        case MODE_CONTAINER:
            msg->container_id = target_value;
            if (msg->container_id.empty()) {
                LOG_WARNING("Sample module '%s' missing container id in capture hint '%s'",
                            module_name.c_str(), alert->capture_hint.c_str());
                return false;
            }
            break;
        default:
            break;
    }

    if (pairs.count("netns")) {
        msg->netns_path = pairs["netns"];
    }

    if (pairs.count("filter")) {
        msg->filter = pairs["filter"];
    }
    if (pairs.count("protocol")) {
        msg->protocol_filter = pairs["protocol"];
    }
    if (pairs.count("protocol_filter")) {
        msg->protocol_filter = pairs["protocol_filter"];
    }
    if (pairs.count("ip")) {
        msg->ip_filter = pairs["ip"];
    }
    if (pairs.count("ip_filter")) {
        msg->ip_filter = pairs["ip_filter"];
    }
    if (pairs.count("port")) {
        int port = 0;
        if (parse_int_value(pairs["port"], port) && port > 0) {
            msg->port_filter = port;
        }
    }
    if (pairs.count("port_filter")) {
        int port = 0;
        if (parse_int_value(pairs["port_filter"], port) && port > 0) {
            msg->port_filter = port;
        }
    }

    if (pairs.count("category")) {
        msg->category = pairs["category"];
    } else if (!alert->capture_category.empty()) {
        msg->category = alert->capture_category;
    }

    int duration_sec = alert->capture_duration_sec;
    if (pairs.count("duration")) {
        int value = 0;
        if (parse_int_value(pairs["duration"], value) && value > 0) {
            duration_sec = value;
        }
    }
    if (pairs.count("duration_sec")) {
        int value = 0;
        if (parse_int_value(pairs["duration_sec"], value) && value > 0) {
            duration_sec = value;
        }
    }
    if (duration_sec <= 0 && defaults) {
        duration_sec = defaults->duration_sec;
    }
    if (duration_sec > 0) {
        msg->duration_sec = duration_sec;
    }

    if (pairs.count("max_bytes")) {
        long value = 0;
        if (parse_long_value(pairs["max_bytes"], value) && value > 0) {
            msg->max_bytes = value;
        }
    } else if (defaults && msg->max_bytes <= 0) {
        msg->max_bytes = defaults->max_bytes;
    }

    if (pairs.count("max_packets")) {
        int value = 0;
        if (parse_int_value(pairs["max_packets"], value) && value > 0) {
            msg->max_packets = value;
        }
    }

    if (msg->category.empty() && defaults) {
        msg->category = defaults->category;
    }
    if (msg->file_pattern.empty() && defaults) {
        msg->file_pattern = defaults->file_pattern;
    }

    msg->client_ip = "sample";
    msg->request_user = "module:" + module_name;
    msg->enqueue_ts_ms = GetMilliSecond();

    start_msg = msg;
    return true;
}

void CRxCaptureManagerThread::handle_timeout(shared_ptr<timer_msg>& t_msg)
{
    if (!t_msg)
        return;

    LOG_DEBUG("handle_timeout timer_type:%d, time_length:%d", t_msg->_timer_type, t_msg->_time_length);

    switch(t_msg->_timer_type)
    {
        case TIMER_TYPE_QUEUE_CHECK:
            check_queue();
            start_queue_timer();
            break;
        case TIMER_TYPE_EXPIRE_CLEAN:
            clean_expired_files();
            start_clean_timer();
            break;
        case TIMER_TYPE_BATCH_COMPRESS:
            batch_compress_files();
            start_compress_timer();
            break;
        default:
            LOG_DEBUG("Unknown timer type: %d", t_msg->_timer_type);
            break;
    }

    malloc_trim(0);
}

bool CRxCaptureManagerThread::resolve_target_processes(
    shared_ptr<SRxStartCaptureMsg>& start_msg,
    std::vector<SProcessInfo>& matched_processes,
    shared_ptr<SRxHttpReplyMsg>& error_reply)
{
    if (start_msg->capture_mode == 1) {
        uint64_t begin_ms = GetMilliSecond();
        LOG_NOTICE("Resolving processes for name: %s", start_msg->proc_name.c_str());
        matched_processes = CRxProcessResolver::FindProcessesByName(start_msg->proc_name);
        uint64_t end_ms = GetMilliSecond();
        LOG_NOTICE("Process resolve finished for %s in %llu ms (matches=%zu)",
                   start_msg->proc_name.c_str(),
                   static_cast<unsigned long long>(end_ms - begin_ms),
                   matched_processes.size());

        if (matched_processes.empty()) {
            error_reply.reset(new SRxHttpReplyMsg());
            error_reply->conn_id = start_msg->reply_target._id;
            error_reply->status = 404;
            error_reply->reason = "Not Found";
            error_reply->headers["Content-Type"] = "application/json";

            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf),
                    "{\"error\":\"process not found\",\"proc_name\":\"%s\"}",
                    start_msg->proc_name.c_str());
            error_reply->body = err_buf;
            return false;
        }

        LOG_NOTICE("Found %zu processes matching '%s'", matched_processes.size(), start_msg->proc_name.c_str());

        if (start_msg->port_filter == 0 && !matched_processes.empty()) {
            for (size_t i = 0; i < matched_processes.size(); ++i) {
                if (!matched_processes[i].listening_ports.empty()) {
                    std::ostringstream port_log;
                    for (size_t j = 0; j < matched_processes[i].listening_ports.size(); ++j) {
                        if (j > 0) {
                            port_log << ',';
                        }
                        port_log << matched_processes[i].listening_ports[j];
                    }
                    LOG_NOTICE("Process %d listening ports: %s",
                               matched_processes[i].pid, port_log.str().c_str());
                }
            }
        }
    } else if (start_msg->capture_mode == 2) {
        SProcessInfo proc_info;
        if (!CRxProcessResolver::GetProcessInfo(static_cast<pid_t>(start_msg->target_pid), proc_info)) {
            error_reply.reset(new SRxHttpReplyMsg());
            error_reply->conn_id = start_msg->reply_target._id;
            error_reply->status = 404;
            error_reply->reason = "Not Found";
            error_reply->headers["Content-Type"] = "application/json";

            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf),
                    "{\"error\":\"pid not found\",\"pid\":%d}",
                    start_msg->target_pid);
            error_reply->body = err_buf;
            return false;
        }
        matched_processes.push_back(proc_info);
    }
    return true;
}

std::string CRxCaptureManagerThread::generate_task_key(const shared_ptr<SRxStartCaptureMsg>& start_msg)
{
    char key_buf[512];
    switch (start_msg->capture_mode) {
        case 0:
            snprintf(key_buf, sizeof(key_buf), "iface:%s:%s",
                    start_msg->iface.c_str(), start_msg->filter.c_str());
            break;
        case 1:
            snprintf(key_buf, sizeof(key_buf), "proc:%s:%s:%d",
                    start_msg->proc_name.c_str(), start_msg->filter.c_str(), start_msg->port_filter);
            break;
        case 2:
            snprintf(key_buf, sizeof(key_buf), "pid:%d", start_msg->target_pid);
            break;
        case 3:
            snprintf(key_buf, sizeof(key_buf), "container:%s:%s",
                    start_msg->container_id.c_str(), start_msg->filter.c_str());
            break;
        default:
            return "unknown";
    }
    return std::string(key_buf);
}

bool CRxCaptureManagerThread::check_task_duplicate(const std::string& task_key, int& existing_id, std::string& existing_status)
{
    CRxProcData* global_data = CRxProcData::instance();
    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();

    if (!task_mgr.is_key_active(task_key)) {
        return false;
    }

    TaskSnapshot snapshot;
    if (task_mgr.query_task_by_key(task_key, snapshot)) {
        existing_id = snapshot.capture_id;
        const char* status_names[] = {"pending", "resolving", "running", "completed", "failed", "stopped"};
        if (snapshot.status >= 0 && snapshot.status <= 5) {
            existing_status = status_names[snapshot.status];
        } else {
            existing_status = "unknown";
        }
    } else {
        existing_id = 0;
        existing_status = "unknown";
    }
    return true;
}

int CRxCaptureManagerThread::get_max_concurrent_limit()
{
    CRxProcData* global_data = CRxProcData::instance();
    int max_concurrent = 0;
    if (global_data && global_data->_strategy_dict) {
        max_concurrent = global_data->_strategy_dict->current()->limits().max_concurrent_captures;
    }
    if (max_concurrent <= 0) {
        max_concurrent = RxCaptureConstants::kMinConcurrentCaptures;
    }
    return max_concurrent;
}

void CRxCaptureManagerThread::count_active_tasks(size_t& running_count, size_t& pending_count)
{
    CRxProcData* global_data = CRxProcData::instance();
    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();

    TaskStats stats = task_mgr.get_stats();

    running_count = stats.running_count + stats.resolving_count;
    pending_count = stats.pending_count;
}

void CRxCaptureManagerThread::create_duplicate_error_reply(
    shared_ptr<SRxHttpReplyMsg>& reply,
    const std::string& task_key,
    const std::string& sid,
    int existing_id,
    const std::string& status)
{
    reply->status = 409;
    reply->reason = "Conflict";
    reply->headers["Content-Type"] = "application/json";

    std::ostringstream body;
    body << '{';
    body << "\"error\":\"duplicate capture task\"";
    body << ",\"key\":\"" << json_escape(task_key) << "\"";
    if (!sid.empty()) {
        body << ",\"sid\":\"" << json_escape(sid) << "\"";
    }
    body << ",\"existing_capture_id\":" << existing_id;
    body << ",\"status\":\"" << json_escape(status) << "\"";
    body << '}';
    reply->body = body.str();

    LOG_NOTICE("Duplicate capture task: key=%s, existing_id=%d, status=%s",
               task_key.c_str(), existing_id, status.c_str());
}

void CRxCaptureManagerThread::create_capacity_error_reply(
    shared_ptr<SRxHttpReplyMsg>& reply,
    int max_concurrent, size_t running_count, size_t pending_count)
{
    reply->status = 429;
    reply->reason = "Too Many Requests";
    reply->headers["Content-Type"] = "application/json";

    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf),
            "{\"error\":\"capture capacity reached\",\"max_concurrent\":%d}",
            max_concurrent);
    reply->body = err_buf;

    LOG_WARNING("Capture task rejected: max concurrent reached (%d), running=%zu pending=%zu",
                max_concurrent, running_count, pending_count);
}

int CRxCaptureManagerThread::create_and_add_capture_task(
    const shared_ptr<SRxStartCaptureMsg>& start_msg,
    const std::string& task_key,
    const std::string& signature,
    const std::string& sid,
    const std::vector<SProcessInfo>& matched_processes,
    shared_ptr<SRxHttpReplyMsg>& reply)
{

    static int next_capture_id = RxCaptureConstants::kCaptureIdStartValue;
    int capture_id = next_capture_id < RxCaptureConstants::kCaptureIdStartValue
        ? RxCaptureConstants::kCaptureIdStartValue : next_capture_id++;

    SRxCaptureTask* task = new SRxCaptureTask();
    task->capture_id = capture_id;
    task->key = task_key;
    task->capture_mode = static_cast<ECaptureMode>(start_msg->capture_mode);

    task->iface = start_msg->iface;
    task->proc_name = start_msg->proc_name;
    task->target_pid = static_cast<pid_t>(start_msg->target_pid);
    task->container_id = start_msg->container_id;
    task->resolved_iface = task->iface;
    task->netns_path = start_msg->netns_path;

    for (size_t i = 0; i < matched_processes.size(); ++i) {
        task->matched_pids.push_back(matched_processes[i].pid);
        if (task->netns_path.empty()) {
            task->netns_path = matched_processes[i].netns_path;
        }
    }

    task->filter = start_msg->filter;
    task->protocol_filter = start_msg->protocol_filter;
    task->ip_filter = start_msg->ip_filter;
    task->port_filter = start_msg->port_filter;

    task->category = start_msg->category;
    task->file_pattern = start_msg->file_pattern;
    task->duration_sec = start_msg->duration_sec;
    task->max_bytes = start_msg->max_bytes;
    task->max_packets = start_msg->max_packets;
    task->priority = 0;
    task->signature = signature;
    task->sid = sid;

    task->reply_target = start_msg->reply_target;
    task->client_ip = start_msg->client_ip;
    task->request_user = start_msg->request_user;

    task->status = STATUS_PENDING;
    task->start_time = time(NULL);

    CRxProcData* global_data = CRxProcData::instance();
    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    task_mgr.add_task(capture_id, task_key, signature, sid, task);

    reply->status = 200;
    reply->reason = "OK";
    reply->headers["Content-Type"] = "application/json";

    char buf[1024];
    const char* mode_name[] = {"interface", "process", "pid", "container"};
    snprintf(buf, sizeof(buf),
            "{\"capture_id\":%d,\"duplicate\":false,\"status\":\"started\","
            "\"mode\":\"%s\",\"key\":\"%s\",\"sid\":\"%s\",\"matched_pids\":%zu,\"port\":%d}",
            capture_id, mode_name[start_msg->capture_mode], task_key.c_str(),
            sid.c_str(), matched_processes.size(), start_msg->port_filter);
    reply->body = buf;

    LOG_NOTICE("Started capture task %d (mode=%s, pids=%zu, port=%d): %s",
              capture_id, mode_name[start_msg->capture_mode],
              matched_processes.size(), start_msg->port_filter, task_key.c_str());

    return capture_id;
}

void CRxCaptureManagerThread::dispatch_task_to_worker(int capture_id,
    const std::string& task_key,
    const std::string& sid,
    const CaptureSpec& spec,
    const CaptureConfigSnapshot& config_snapshot,
    shared_ptr<SRxStartCaptureMsg>& start_msg)
{
    if (_worker_thd_vec.empty()) {
        return;
    }

    static size_t next_worker = 0;
    size_t worker_index = next_worker % _worker_thd_vec.size();
    next_worker++;

    ObjId worker_target;
    worker_target._id = OBJ_ID_THREAD;
    worker_target._thread_index = _worker_thd_vec[worker_index];

    shared_ptr<SRxCaptureStartMsgV2> start_v2(new SRxCaptureStartMsgV2());
    start_v2->capture_id = capture_id;
    start_v2->key = task_key;
    start_v2->sid = sid;
    start_v2->config_hash = config_snapshot.config_hash;
    start_v2->sender_thread_index = static_cast<int>(get_thread_index());
    start_v2->config = config_snapshot;
    start_v2->spec = spec;
    start_v2->worker_id = worker_target._thread_index;

    shared_ptr<normal_msg> start_v2_base =
        static_pointer_cast<normal_msg>(start_v2);
    base_net_thread::put_obj_msg(worker_target, start_v2_base);

    if (start_msg) {
        start_msg->sid = sid;
        shared_ptr<normal_msg> legacy =
            static_pointer_cast<normal_msg>(start_msg);
        base_net_thread::put_obj_msg(worker_target, legacy);
    }

    LOG_NOTICE("Dispatched capture task %d to worker thread %u",
              capture_id, worker_target._thread_index);

    CRxProcData* global_data = CRxProcData::instance();
    if (global_data) {
        CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
        AssignWorkerFunctor updater(worker_target._thread_index);
        task_mgr.update_task(capture_id, updater);
    }
}

void CRxCaptureManagerThread::send_reply_to_http(const ObjId& reply_target, shared_ptr<SRxHttpReplyMsg>& reply)
{
    if (reply_target._thread_index > 0) {
        shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(reply);
        ObjId target = reply_target;
        base_net_thread::put_obj_msg(target, base);
    }
}

void CRxCaptureManagerThread::handle_start_capture(shared_ptr<normal_msg>& msg)
{
    LOG_DEBUG("handle_start_capture");

    shared_ptr<SRxStartCaptureMsg> start_msg =
        dynamic_pointer_cast<SRxStartCaptureMsg>(msg);

    if (!start_msg) {
        LOG_WARNING("handle_start_capture: invalid message type");
        return;
    }

    std::vector<SProcessInfo> matched_processes;
    shared_ptr<SRxHttpReplyMsg> error_reply;
    if (!resolve_target_processes(start_msg, matched_processes, error_reply)) {
        send_reply_to_http(start_msg->reply_target, error_reply);
        return;
    }

    if (start_msg->capture_mode == MODE_PROCESS && start_msg->filter.empty()
        && !start_msg->proc_name.empty()) {
        std::set<int> ports;
        for (size_t i = 0; i < matched_processes.size(); ++i) {
            const SProcessInfo& info = matched_processes[i];
            for (size_t j = 0; j < info.listening_ports.size(); ++j) {
                if (info.listening_ports[j] > 0) {
                    ports.insert(info.listening_ports[j]);
                }
            }
        }

        if (!ports.empty()) {
            std::ostringstream bpf;
            size_t idx = 0;
            for (std::set<int>::const_iterator it = ports.begin(); it != ports.end(); ++it, ++idx) {
                if (idx > 0) {
                    bpf << " or ";
                }
                bpf << "port " << *it;
            }
            start_msg->filter = bpf.str();
            /* Only set a single port_filter when there is exactly one port;
             * otherwise leave it 0 to indicate "multiple ports" while BPF handles all. */
            if (start_msg->port_filter == 0 && ports.size() == 1) {
                start_msg->port_filter = *ports.begin();
            }
            LOG_NOTICE("Auto-generated BPF for process '%s': %s",
                       start_msg->proc_name.c_str(), start_msg->filter.c_str());
        } else {
            LOG_WARNING("Process '%s' has no listening ports; proceeding without auto BPF",
                        start_msg->proc_name.c_str());
        }
    }

    if (start_msg->port_filter <= 0 && !start_msg->filter.empty()) {
        int inferred_port = infer_port_from_filter(start_msg->filter);
        if (inferred_port > 0) {
            start_msg->port_filter = inferred_port;
        }
    }

    std::string task_key = generate_task_key(start_msg);

    CRxProcData* proc_data = CRxProcData::instance();
    CaptureConfigSnapshot config_snapshot = proc_data->get_capture_config_snapshot();
    CRxStrategyConfigManager* strategy_cfg = proc_data ? proc_data->current_strategy_config() : NULL;

    if (start_msg->iface.empty()) {
        if (strategy_cfg) {
            start_msg->iface = strategy_cfg->get_default_iface();
        }
        if (start_msg->iface.empty()) {
            start_msg->iface = "any";
        }
    }

    int effective_duration = start_msg->duration_sec;
    if (effective_duration <= 0) {
        effective_duration = config_snapshot.max_duration_sec;
    }
    if (effective_duration <= 0 && strategy_cfg) {
        effective_duration = strategy_cfg->get_default_duration();
    }
    if (effective_duration <= 0) {
        effective_duration = kFallbackDurationSec;
    }
    start_msg->duration_sec = effective_duration;
    if (config_snapshot.max_duration_sec <= 0) {
        config_snapshot.max_duration_sec = effective_duration;
    }

    CaptureSpec capture_spec;
    capture_spec.capture_mode = static_cast<ECaptureMode>(start_msg->capture_mode);
    capture_spec.iface = start_msg->iface;
    capture_spec.resolved_iface = start_msg->iface;
    capture_spec.proc_name = start_msg->proc_name;
    capture_spec.target_pid = static_cast<pid_t>(start_msg->target_pid);
    capture_spec.container_id = start_msg->container_id;
    capture_spec.netns_path = start_msg->netns_path;
    capture_spec.category = start_msg->category;
    capture_spec.filter = start_msg->filter;
    capture_spec.protocol_filter = start_msg->protocol_filter;
    capture_spec.ip_filter = start_msg->ip_filter;
    capture_spec.port_filter = start_msg->port_filter;
    capture_spec.output_pattern = start_msg->file_pattern;
    capture_spec.max_duration_sec = effective_duration;
    capture_spec.max_bytes = start_msg->max_bytes;
    capture_spec.max_packets = start_msg->max_packets;
    capture_spec.snaplen = config_snapshot.snaplen;

    if (!matched_processes.empty()) {
        if (capture_spec.netns_path.empty()) {
            capture_spec.netns_path = matched_processes[0].netns_path;
        }
    }

    std::string signature_payload = build_capture_signature(capture_spec, config_snapshot, matched_processes);
    unsigned long long fnv = 1469598103934665603ULL;
    for (size_t idx = 0; idx < signature_payload.size(); ++idx) {
        fnv ^= static_cast<unsigned char>(signature_payload[idx]);
        fnv *= 1099511628211ULL;
    }
    std::ostringstream sig_hex;
    sig_hex << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << fnv;
    std::string signature = sig_hex.str();
    std::string sid = make_sid_with_timestamp(signature);

    int existing_id = 0;
    std::string existing_status;
    bool signature_duplicate = false;
    TaskSnapshot signature_snapshot;

    CRxProcData* global_data = CRxProcData::instance();
    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();

    if (task_mgr.query_active_task_by_signature(signature, signature_snapshot)) {
        signature_duplicate = true;
        existing_id = signature_snapshot.capture_id;
        existing_status = capture_status_to_string(signature_snapshot.status);
    }

    bool is_duplicate = false;
    TaskSnapshot existing_snapshot;
    if (!signature_duplicate) {
        is_duplicate = check_task_duplicate(task_key, existing_id, existing_status);
        if (is_duplicate && existing_id > 0) {
            task_mgr.query_task(existing_id, existing_snapshot);
        }
    } else {
        existing_snapshot = signature_snapshot;
    }

    size_t running_count = 0;
    size_t pending_count = 0;
    count_active_tasks(running_count, pending_count);

    int max_concurrent = get_max_concurrent_limit();

    shared_ptr<SRxHttpReplyMsg> reply(new SRxHttpReplyMsg());
    reply->conn_id = start_msg->reply_target._id;

    int capture_id = -1;
    bool accepted = false;

    if (signature_duplicate) {
        const std::string& duplicate_key = existing_snapshot.key.empty() ? task_key : existing_snapshot.key;
        const std::string& duplicate_sid = existing_snapshot.sid.empty() ? sid : existing_snapshot.sid;
        create_duplicate_error_reply(reply, duplicate_key, duplicate_sid, existing_id, existing_status);
    } else if (is_duplicate) {
        const std::string& duplicate_key = existing_snapshot.key.empty() ? task_key : existing_snapshot.key;
        const std::string& duplicate_sid = existing_snapshot.sid.empty() ? sid : existing_snapshot.sid;
        create_duplicate_error_reply(reply, duplicate_key, duplicate_sid, existing_id, existing_status);
    } else if (running_count >= static_cast<size_t>(max_concurrent)) {
        create_capacity_error_reply(reply, max_concurrent, running_count, pending_count);
    } else {

        capture_id = create_and_add_capture_task(start_msg, task_key, signature, sid, matched_processes, reply);

        dispatch_task_to_worker(capture_id, task_key, sid, capture_spec, config_snapshot, start_msg);
        accepted = (capture_id > 0);
    }

    uint64_t reply_ready_ms = GetMilliSecond();
    reply->debug_request_ts_ms = start_msg->enqueue_ts_ms;
    reply->debug_reply_ts_ms = reply_ready_ms;
    if (start_msg->enqueue_ts_ms > 0) {
        uint64_t delta = reply_ready_ms - start_msg->enqueue_ts_ms;
        char debug_buf[32];
        snprintf(debug_buf, sizeof(debug_buf), "%llu",
                 static_cast<unsigned long long>(delta));
        reply->headers["X-Debug-QueueMs"] = debug_buf;
        LOG_NOTICE_MSG("Start-capture reply prepared: capture_id=%d accepted=%d queue_wait_ms=%llu",
                       capture_id,
                       accepted ? 1 : 0,
                       static_cast<unsigned long long>(delta));
    }

    send_reply_to_http(start_msg->reply_target, reply);
}

void CRxCaptureManagerThread::handle_stop_capture(shared_ptr<normal_msg>& msg)
{
    LOG_DEBUG("handle_stop_capture");

    shared_ptr<SRxStopCaptureMsg> stop_msg =
        dynamic_pointer_cast<SRxStopCaptureMsg>(msg);

    if (!stop_msg) {
        LOG_WARNING("handle_stop_capture: invalid message type");
        return;
    }

    shared_ptr<SRxHttpReplyMsg> reply(new SRxHttpReplyMsg());
    reply->conn_id = stop_msg->reply_target._id;
    reply->headers["Content-Type"] = "application/json";

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        reply->status = 500;
        reply->reason = "Error";
        reply->body = "{\"error\":\"internal error\"}";
        send_reply_to_http(stop_msg->reply_target, reply);
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    TaskSnapshot snapshot;
    if (!task_mgr.query_task(stop_msg->capture_id, snapshot)) {
        reply->status = 404;
        reply->reason = "Not Found";
        char buf[256];
        snprintf(buf, sizeof(buf),
                      "{\"error\":\"capture_not_found\",\"capture_id\":%d}",
                      stop_msg->capture_id);
        reply->body = buf;
        send_reply_to_http(stop_msg->reply_target, reply);
        return;
    }

    const char* status_str = capture_status_to_string(snapshot.status);
    std::string snapshot_key = json_escape(snapshot.key);
    std::string snapshot_sid = json_escape(snapshot.sid);

    if (snapshot.status == STATUS_COMPLETED ||
        snapshot.status == STATUS_FAILED ||
        snapshot.status == STATUS_STOPPED) {
        reply->status = 200;
        reply->reason = "OK";
        std::ostringstream oss;
        oss << "{\"capture_id\":" << snapshot.capture_id
            << ",\"key\":\"" << snapshot_key << "\""
            << ",\"sid\":\"" << snapshot_sid << "\""
            << ",\"status\":\"" << status_str << "\"}";
        reply->body = oss.str();
        send_reply_to_http(stop_msg->reply_target, reply);
        return;
    }

    MarkStopFunctor mark_stop(true);
    task_mgr.update_task(stop_msg->capture_id, mark_stop);

    bool dispatched = false;
    if (snapshot.worker_thread_index > 0) {
        ObjId target;
        target._id = OBJ_ID_THREAD;
        target._thread_index = snapshot.worker_thread_index;

        shared_ptr<SRxCaptureStopMsgV2> stop_v2(new SRxCaptureStopMsgV2());
        stop_v2->capture_id = snapshot.capture_id;
        stop_v2->key = snapshot.key;
        stop_v2->sid = snapshot.sid;
        stop_v2->op_version = 1;
        stop_v2->config_hash = 0;
        stop_v2->sender_thread_index = static_cast<int>(get_thread_index());
        stop_v2->stop_reason = "user_stop";

        shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(stop_v2);
        base_net_thread::put_obj_msg(target, base);
        dispatched = true;
    } else {
        task_mgr.update_status(stop_msg->capture_id, STATUS_STOPPED);
    }

    reply->status = 200;
    reply->reason = "OK";
    std::ostringstream oss;
    oss << "{\"capture_id\":" << snapshot.capture_id
        << ",\"key\":\"" << snapshot_key << "\""
        << ",\"sid\":\"" << snapshot_sid << "\""
        << ",\"status\":\"" << (dispatched ? "stopping" : "stopped") << "\""
        << ",\"dispatched\":" << (dispatched ? "true" : "false") << "}";
    reply->body = oss.str();

    LOG_NOTICE("Stop requested for capture %d (worker=%u, dispatched=%d)",
               snapshot.capture_id, snapshot.worker_thread_index ? snapshot.worker_thread_index : 0, dispatched ? 1 : 0);

    send_reply_to_http(stop_msg->reply_target, reply);
}

void CRxCaptureManagerThread::handle_query_capture(shared_ptr<normal_msg>& msg)
{
    LOG_DEBUG("handle_query_capture");

    shared_ptr<SRxQueryCaptureMsg> query_msg =
        dynamic_pointer_cast<SRxQueryCaptureMsg>(msg);

    if (!query_msg) {
        LOG_WARNING("handle_query_capture: invalid message type");
        return;
    }

    shared_ptr<SRxHttpReplyMsg> reply(new SRxHttpReplyMsg());
    reply->conn_id = query_msg->reply_target._id;
    reply->headers["Content-Type"] = "application/json";

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        reply->status = 500;
        reply->reason = "Error";
        reply->body = "{\"error\":\"internal error\"}";
        send_reply_to_http(query_msg->reply_target, reply);
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    TaskSnapshot snapshot;
    if (!task_mgr.query_task(query_msg->capture_id, snapshot)) {
        reply->status = 404;
        reply->reason = "Not Found";
        char buf[256];
        snprintf(buf, sizeof(buf),
                      "{\"error\":\"capture_not_found\",\"capture_id\":%d}",
                      query_msg->capture_id);
        reply->body = buf;
        send_reply_to_http(query_msg->reply_target, reply);
        return;
    }

    reply->status = 200;
    reply->reason = "OK";

    std::ostringstream oss;
    oss << "{"
        << "\"capture_id\":" << snapshot.capture_id
        << ",\"status\":\"" << capture_status_to_string(snapshot.status) << "\""
        << ",\"mode\":\"" << capture_mode_to_string(snapshot.capture_mode) << "\""
        << ",\"key\":\"" << json_escape(snapshot.key) << "\""
        << ",\"sid\":\"" << json_escape(snapshot.sid) << "\"";

    if (!snapshot.iface.empty()) {
        oss << ",\"iface\":\"" << json_escape(snapshot.iface) << "\"";
    }
    if (!snapshot.proc_name.empty()) {
        oss << ",\"proc_name\":\"" << json_escape(snapshot.proc_name) << "\"";
    }
    if (!snapshot.filter.empty()) {
        oss << ",\"filter\":\"" << json_escape(snapshot.filter) << "\"";
    }
    if (snapshot.target_pid > 0) {
        oss << ",\"pid\":" << snapshot.target_pid;
    }
    if (snapshot.port_filter > 0) {
        oss << ",\"port\":" << snapshot.port_filter;
    }
    oss << ",\"start_time\":" << snapshot.start_time
        << ",\"end_time\":" << snapshot.end_time
        << ",\"packets\":" << snapshot.packet_count
        << ",\"bytes\":" << snapshot.bytes_captured
        << ",\"worker\":" << snapshot.worker_thread_index
        << ",\"stop_requested\":" << (snapshot.stop_requested ? "true" : "false")
        << ",\"client_ip\":\"" << json_escape(snapshot.client_ip) << "\""
        << ",\"request_user\":\"" << json_escape(snapshot.request_user) << "\"";

    if (!snapshot.error_message.empty()) {
        oss << ",\"error\":\"" << json_escape(snapshot.error_message) << "\"";
    }
    if (!snapshot.captured_files.empty()) {
        oss << ",\"files\":[";
        for (size_t i = 0; i < snapshot.captured_files.size(); ++i) {
            const CaptureFileInfo& info = snapshot.captured_files[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{\"path\":\"" << json_escape(info.file_path) << "\"";
            oss << ",\"size\":" << info.file_size;
            oss << ",\"segment\":" << info.segment_index;
            oss << ",\"segments\":" << info.total_segments;
            oss << ",\"compressed\":" << (info.compressed ? "true" : "false");
            if (!info.archive_path.empty()) {
                oss << ",\"archive\":\"" << json_escape(info.archive_path) << "\"";
            }
            if (info.compress_finish_ts > 0) {
                oss << ",\"compressed_at\":" << info.compress_finish_ts;
            }
            if (!info.record_path.empty()) {
                oss << ",\"record\":\"" << json_escape(info.record_path) << "\"";
            }
            oss << "}";
        }
        oss << "]";
    }
    if (!snapshot.archives.empty()) {
        oss << ",\"archives\":[";
        for (size_t i = 0; i < snapshot.archives.size(); ++i) {
            const CaptureArchiveInfo& arc = snapshot.archives[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{\"path\":\"" << json_escape(arc.archive_path) << "\"";
            oss << ",\"size\":" << arc.archive_size;
            if (arc.compress_finish_ts > 0) {
                oss << ",\"compressed_at\":" << arc.compress_finish_ts;
            }
            if (!arc.files.empty()) {
                oss << ",\"files\":[";
                for (size_t j = 0; j < arc.files.size(); ++j) {
                    if (j > 0) {
                        oss << ",";
                    }
                    oss << "\"" << json_escape(arc.files[j].file_path) << "\"";
                }
                oss << "]";
            }
            oss << "}";
        }
        oss << "]";
    }
    oss << "}";

    reply->body = oss.str();

    LOG_DEBUG("Query capture %d status=%s packets=%lu bytes=%lu",
              snapshot.capture_id,
              capture_status_to_string(snapshot.status),
              snapshot.packet_count,
              snapshot.bytes_captured);

    send_reply_to_http(query_msg->reply_target, reply);
}

struct UpdateTaskFunctor {
    shared_ptr<SRxTaskUpdateMsg> msg;

    explicit UpdateTaskFunctor(shared_ptr<SRxTaskUpdateMsg> m)
        : msg(m)
    {
    }

    void operator()(SRxCaptureTask& task) const
    {
        task.status = msg->new_status;

        if (msg->update_capture_pid) {
            task.capture_pid = msg->capture_pid;
        }
        if (msg->update_output_file) {
            task.output_file = msg->output_file;
        }
        if (msg->update_start_time) {
            task.start_time = msg->start_time;
        }
        if (msg->update_end_time) {
            task.end_time = msg->end_time;
        }
        if (msg->update_stats) {
            task.packet_count = msg->packet_count;
            task.bytes_captured = msg->bytes_captured;
        }
        if (msg->update_error) {
            task.error_message = msg->error_message;
        }
    }
};

void CRxCaptureManagerThread::handle_task_update(shared_ptr<normal_msg>& msg)
{
    LOG_DEBUG("handle_task_update");

    shared_ptr<SRxTaskUpdateMsg> update_msg =
        dynamic_pointer_cast<SRxTaskUpdateMsg>(msg);

    if (!update_msg) {
        LOG_WARNING("handle_task_update: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        LOG_ERROR("handle_task_update: global_data is NULL");
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();

    bool handled = false;

    if (update_msg->new_status == STATUS_RUNNING && update_msg->update_start_time) {
        int64_t start_ts_usec = static_cast<int64_t>(update_msg->start_time) * 1000000LL;
        const std::string& file_path = update_msg->update_output_file ? update_msg->output_file : "";
        if (task_mgr.set_capture_started(update_msg->capture_id, start_ts_usec,
                                         update_msg->update_capture_pid ? update_msg->capture_pid : -1,
                                         file_path)) {
            LOG_NOTICE("Task %d entered RUNNING state", update_msg->capture_id);
            handled = true;
        }
    }

    if (update_msg->update_stats) {
        int64_t last_ts_usec = update_msg->update_end_time
            ? static_cast<int64_t>(update_msg->end_time) * 1000000LL
            : 0;
        if (task_mgr.update_progress(update_msg->capture_id,
                                     update_msg->packet_count,
                                     update_msg->bytes_captured,
                                     last_ts_usec)) {
            handled = true;
        }
    }

    if (update_msg->new_status == STATUS_COMPLETED) {
        int64_t finish_ts_usec = update_msg->update_end_time
            ? static_cast<int64_t>(update_msg->end_time) * 1000000LL
            : rx_capture_now_usec();
        const std::string& final_path = update_msg->update_output_file ? update_msg->output_file : "";
        if (task_mgr.set_capture_finished(update_msg->capture_id,
                                          finish_ts_usec,
                                          update_msg->packet_count,
                                          update_msg->bytes_captured,
                                          final_path)) {
            LOG_NOTICE("Task %d completed successfully", update_msg->capture_id);
            handled = true;
        }
    } else if (update_msg->new_status == STATUS_FAILED && update_msg->update_error) {
        if (task_mgr.set_capture_failed(update_msg->capture_id, update_msg->error_message)) {
            LOG_WARNING("Task %d failed: %s",
                        update_msg->capture_id,
                        update_msg->error_message.c_str());
            handled = true;
        }
    }

    if (!handled) {
        bool success = task_mgr.update_task(update_msg->capture_id,
            UpdateTaskFunctor(update_msg));

        if (success) {
            LOG_NOTICE("Task %d updated with multiple fields", update_msg->capture_id);
        } else {
            LOG_WARNING("Task %d update failed (not found)", update_msg->capture_id);
        }
    }
}

void CRxCaptureManagerThread::handle_capture_started_v2(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCaptureStartedMsgV2> started =
        dynamic_pointer_cast<SRxCaptureStartedMsgV2>(msg);
    if (!started) {
        LOG_WARNING("handle_capture_started_v2: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    task_mgr.set_capture_started(started->capture_id,
                                 started->start_ts,
                                 started->capture_pid,
                                 started->output_file);

    LOG_NOTICE("Task %d reported RUNNING by worker %d",
               started->capture_id, started->sender_thread_index);
}

void CRxCaptureManagerThread::handle_capture_progress_v2(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCaptureProgressMsgV2> progress =
        dynamic_pointer_cast<SRxCaptureProgressMsgV2>(msg);
    if (!progress) {
        LOG_WARNING("handle_capture_progress_v2: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    int64_t last_ts = progress->progress.last_packet_ts != 0
        ? progress->progress.last_packet_ts
        : progress->report_ts;

    task_mgr.update_progress(progress->capture_id,
                             progress->progress.packets,
                             progress->progress.bytes,
                             last_ts);

    LOG_DEBUG("Task %d progress: packets=%lu bytes=%lu",
              progress->capture_id,
              progress->progress.packets,
              progress->progress.bytes);
}

void CRxCaptureManagerThread::handle_capture_file_ready_v2(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCaptureFileReadyMsgV2> ready =
        dynamic_pointer_cast<SRxCaptureFileReadyMsgV2>(msg);
    if (!ready) {
        LOG_WARNING("handle_capture_file_ready_v2: invalid message type");
        return;
    }

    if (ready->files.empty()) {
        return;
    }

    for (size_t i = 0; i < ready->files.size(); ++i) {
        const CaptureFileInfo& info = ready->files[i];
        LOG_NOTICE("Task %d captured file %s (size=%lu)",
                   ready->capture_id, info.file_path.c_str(), info.file_size);
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    task_mgr.append_capture_files(ready->capture_id, ready->files);

    CRxCleanupThread* cleanup_thread = global_data->get_cleanup_thread();
    if (!cleanup_thread) {
        LOG_WARNING("Cleanup thread not available; captured files will not be compressed");
        return;
    }

    shared_ptr<SRxFileEnqueueMsgV2> enqueue(new SRxFileEnqueueMsgV2());
    enqueue->capture_id = ready->capture_id;
    enqueue->key = ready->key;
    enqueue->sid = ready->sid;
    enqueue->op_version = ready->op_version;
    enqueue->config_hash = ready->config_hash;
    enqueue->sender_thread_index = static_cast<int>(get_thread_index());
    enqueue->files = ready->files;
    enqueue->clean_policy = global_data->get_capture_config_snapshot();

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = cleanup_thread->get_thread_index();

    shared_ptr<normal_msg> base_msg =
        static_pointer_cast<normal_msg>(enqueue);
    base_net_thread::put_obj_msg(target, base_msg);

    LOG_NOTICE("Enqueued %zu capture file(s) for cleanup thread (capture_id=%d)",
               ready->files.size(), ready->capture_id);
}

void CRxCaptureManagerThread::handle_capture_finished_v2(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCaptureFinishedMsgV2> finished =
        dynamic_pointer_cast<SRxCaptureFinishedMsgV2>(msg);
    if (!finished) {
        LOG_WARNING("handle_capture_finished_v2: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();

    if (finished->result.exit_code == 0) {
        task_mgr.set_capture_finished(finished->capture_id,
                                      finished->result.finish_ts,
                                      finished->result.total_packets,
                                      finished->result.total_bytes,
                                      std::string());

        LOG_NOTICE("Task %d completed: packets=%lu bytes=%lu",
                   finished->capture_id,
                   finished->result.total_packets,
                   finished->result.total_bytes);
    } else if (finished->result.exit_code == ERR_RUN_CANCELLED) {
        MarkStoppedFunctor updater(finished->result.finish_ts,
                                   finished->result.total_packets,
                                   finished->result.total_bytes,
                                   finished->result.error_message.empty() ? "stopped" : finished->result.error_message);

        task_mgr.update_task(finished->capture_id, updater);
        task_mgr.update_status(finished->capture_id, STATUS_STOPPED);

        LOG_NOTICE("Task %d stopped by user", finished->capture_id);
    } else {

        std::string err = finished->result.error_message;
        if (err.empty()) {
            err = "capture_failed";
        }
        task_mgr.set_capture_failed(finished->capture_id, err);
        LOG_WARNING("Task %d finished with unexpected code=%d", finished->capture_id, finished->result.exit_code);
    }

    clear_module_cooldown_for_capture(finished->capture_id);
}

void CRxCaptureManagerThread::handle_capture_failed_v2(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCaptureFailedMsgV2> failed =
        dynamic_pointer_cast<SRxCaptureFailedMsgV2>(msg);
    if (!failed) {
        LOG_WARNING("handle_capture_failed_v2: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    std::string message = failed->error_message;
    if (message.empty()) {
        message = rx_capture_error_to_string(failed->error_code);
    }
    task_mgr.set_capture_failed(failed->capture_id, message);

    LOG_WARNING("Task %d failed: %s", failed->capture_id, message.c_str());

    clear_module_cooldown_for_capture(failed->capture_id);
}

void CRxCaptureManagerThread::handle_clean_compress_done(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCleanCompressDoneMsgV2> done =
        dynamic_pointer_cast<SRxCleanCompressDoneMsgV2>(msg);
    if (!done) {
        LOG_WARNING("handle_clean_compress_done: invalid message type");
        return;
    }

    CRxProcData* global_data = CRxProcData::instance();
    if (!global_data) {
        return;
    }

    CRxSafeTaskMgr& task_mgr = global_data->capture_task_mgr();
    CaptureArchiveInfo archive;
    archive.archive_path = done->archive_path;
    archive.archive_size = done->compressed_bytes;
    archive.compress_finish_ts = done->compressed_files.empty()
        ? (done->ts_usec / 1000000LL)
        : done->compressed_files[0].compress_finish_ts;
    archive.files = done->compressed_files;

    task_mgr.record_archive(done->capture_id, archive);

    LOG_NOTICE("Cleanup reported archive %s for capture %d", archive.archive_path.c_str(), done->capture_id);
}

void CRxCaptureManagerThread::handle_clean_compress_failed(shared_ptr<normal_msg>& msg)
{
    shared_ptr<SRxCleanCompressFailedMsgV2> failed =
        dynamic_pointer_cast<SRxCleanCompressFailedMsgV2>(msg);
    if (!failed) {
        LOG_WARNING("handle_clean_compress_failed: invalid message type");
        return;
    }

    LOG_WARNING("Cleanup compression failed for capture %d: %s", failed->capture_id, failed->error_message.c_str());
}

void CRxCaptureManagerThread::handle_clean_expired(shared_ptr<normal_msg>& msg)
{
    (void)msg;
    LOG_DEBUG("handle_clean_expired from message");
    clean_expired_files();
}

void CRxCaptureManagerThread::handle_compress_files(shared_ptr<normal_msg>& msg)
{
    (void)msg;
    LOG_DEBUG("handle_compress_files from message");
    batch_compress_files();
}

void CRxCaptureManagerThread::handle_check_threshold(shared_ptr<normal_msg>& msg)
{
    (void)msg;
    LOG_DEBUG("handle_check_threshold from message");
    check_system_threshold();
}

void CRxCaptureManagerThread::start_queue_timer()
{
    CRxProcData* p_data = CRxProcData::instance();
    if (p_data && p_data->_strategy_dict)
    {
        shared_ptr<timer_msg> t_msg(new timer_msg);
        t_msg->_timer_type = TIMER_TYPE_QUEUE_CHECK;
        t_msg->_time_length = p_data->_strategy_dict->current()->queue_timer_interval_ms();
        t_msg->_obj_id = OBJ_ID_THREAD;
        add_timer(t_msg);

        LOG_DEBUG("Queue timer started with interval: %d ms", t_msg->_time_length);
    }
}

void CRxCaptureManagerThread::start_clean_timer()
{
    CRxProcData* p_data = CRxProcData::instance();
    if (p_data && p_data->_strategy_dict)
    {
        shared_ptr<timer_msg> t_msg(new timer_msg);
        t_msg->_timer_type = TIMER_TYPE_EXPIRE_CLEAN;
        t_msg->_time_length = 3600000;
        t_msg->_obj_id = OBJ_ID_THREAD;
        add_timer(t_msg);

        LOG_DEBUG("Clean timer started with interval: 1 hour");
    }
}

void CRxCaptureManagerThread::start_compress_timer()
{
    CRxProcData* p_data = CRxProcData::instance();
    if (p_data && p_data->_strategy_dict)
    {
        shared_ptr<timer_msg> t_msg(new timer_msg);
        t_msg->_timer_type = TIMER_TYPE_BATCH_COMPRESS;
        t_msg->_time_length = p_data->_strategy_dict->current()->batch_compress_interval_sec() * 1000;
        t_msg->_obj_id = OBJ_ID_THREAD;
        add_timer(t_msg);

        LOG_DEBUG("Compress timer started with interval: %d sec",
                  p_data->_strategy_dict->current()->batch_compress_interval_sec());
    }
}

void CRxCaptureManagerThread::check_queue()
{
    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data)
        return;

    CRxSafeTaskMgr& task_mgr = p_data->capture_task_mgr();

    task_mgr.cleanup_pending_deletes();

    TaskStats stats = task_mgr.get_stats();
    size_t running_size = stats.running_count + stats.resolving_count;
    size_t pending_size = stats.pending_count;

    LOG_DEBUG("check_queue: running=%zu, pending=%zu",
              running_size, pending_size);

    if (!p_data->_strategy_dict)
        return;

    int max_concurrent = p_data->_strategy_dict->current()->limits().max_concurrent_captures;
    LOG_DEBUG("max_concurrent_captures: %d", max_concurrent);

    if ((int)running_size < max_concurrent && pending_size > 0)
    {
        LOG_NOTICE("Can start new capture task, pending queue has tasks");

    }
}

void CRxCaptureManagerThread::clean_expired_files()
{
    LOG_DEBUG("clean_expired_files");

    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data || !p_data->_strategy_dict)
        return;

    const SRxStorage& storage = p_data->_strategy_dict->current()->storage();
    LOG_DEBUG("Cleaning files older than %d days from %s",
              storage.max_age_days, storage.base_dir.c_str());

}

void CRxCaptureManagerThread::batch_compress_files()
{
    LOG_DEBUG("batch_compress_files");

    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data || !p_data->_strategy_dict)
        return;

    const SRxStorage& storage = p_data->_strategy_dict->current()->storage();
    if (!storage.compress_enabled)
    {
        LOG_DEBUG("Compression is disabled in config");
        return;
    }

    LOG_DEBUG("Compressing pcap files with command: %s", storage.compress_cmd.c_str());

}

void CRxCaptureManagerThread::check_system_threshold()
{
    LOG_DEBUG("check_system_threshold");

    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data || !p_data->_strategy_dict)
        return;

    const SRxThresholds& thresholds = p_data->_strategy_dict->current()->thresholds();
    LOG_DEBUG("Thresholds: CPU=%d%%, MEM=%d%%, NET_RX=%d Kbps",
              thresholds.cpu_pct_gt, thresholds.mem_pct_gt, thresholds.net_rx_kbps_gt);

}

bool CRxCaptureManagerThread::GetTaskById(int capture_id, SRxCaptureTask& task)
{
    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data)
        return false;

    CRxSafeTaskMgr& task_mgr = p_data->capture_task_mgr();

    TaskSnapshot snapshot;
    if (!task_mgr.query_task(capture_id, snapshot)) {
        return false;
    }

    task.capture_id = snapshot.capture_id;
    task.key = snapshot.key;
    task.status = snapshot.status;
    task.start_time = snapshot.start_time;
    task.end_time = snapshot.end_time;
    task.packet_count = snapshot.packet_count;
    task.bytes_captured = snapshot.bytes_captured;
    task.error_message = snapshot.error_message;
    task.client_ip = snapshot.client_ip;
    task.request_user = snapshot.request_user;

    return true;
}

bool CRxCaptureManagerThread::UpdateTaskStatus(int capture_id, ECaptureTaskStatus new_status)
{
    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data)
        return false;

    CRxSafeTaskMgr& task_mgr = p_data->capture_task_mgr();

    return task_mgr.update_status(capture_id, new_status);
}

#include "rxurlhandlers.h"

#include "rxcapturemessages.h"
#include "legacy_core.h"
#include "rxprocdata.h"
#include "rxcapturemanagerthread.h"
#include "rxsafetaskmgr.h"
#include "rxprocessresolver.h"
#include "pdef/parser.h"
#include "runtime/protocol.h"

#include "rapidjson/document.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <sstream>
#include <ctime>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <dirent.h>

namespace {

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

static std::string json_escape(const std::string& input)
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

static void set_json_response(http_res_head_para* res_head,
                              std::string* send_body,
                              int status,
                              const std::string& reason,
                              const std::string& body)
{
    if (!res_head || !send_body) {
        return;
    }
    res_head->_response_code = status;
    res_head->_response_str = reason;
    res_head->_headers.clear();
    res_head->_headers["Content-Type"] = "application/json";
    res_head->_headers["Connection"] = "close";
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", body.size());
    res_head->_headers["Content-Length"] = len_buf;
    *send_body = body;
}

static std::map<std::string, std::string> parse_query_params(const std::string& url)
{
    std::map<std::string, std::string> params;
    size_t qpos = url.find('?');
    if (qpos == std::string::npos || qpos + 1 >= url.size()) {
        return params;
    }

    size_t pos = qpos + 1;
    while (pos < url.size()) {
        size_t amp = url.find('&', pos);
        std::string token;
        if (amp == std::string::npos) {
            token = url.substr(pos);
            pos = url.size();
        } else {
            token = url.substr(pos, amp - pos);
            pos = amp + 1;
        }

        if (token.empty()) {
            continue;
        }

        size_t eq = token.find('=');
        std::string key;
        std::string value;
        if (eq == std::string::npos) {
            key = token;
        } else {
            key = token.substr(0, eq);
            value = token.substr(eq + 1);
        }

        if (!key.empty()) {
            params[key] = value;
        }
    }

    return params;
}

static std::string get_configured_pdef_dir()
{
    const char* fallback = "/tmp/rxtracenetcap_pdef";
    CRxProcData* pdata = CRxProcData::instance();
    if (!pdata || !pdata->server_config()) {
        return fallback;
    }
    const std::string& cfg_dir = pdata->server_config()->cleanup().pdef_dir;
    return cfg_dir.empty() ? fallback : cfg_dir;
}

static void append_pdef_dirs(std::vector<std::string>& out)
{
    const char* builtin = "config/protocols";
    if (std::find(out.begin(), out.end(), builtin) == out.end()) {
        out.push_back(builtin);
    }
    std::string configured = get_configured_pdef_dir();
    if (!configured.empty()
        && std::find(out.begin(), out.end(), configured) == out.end()) {
        out.push_back(configured);
    }
}

}

CRxUrlHandlerStaticJson::CRxUrlHandlerStaticJson(int code,
                                                 const std::string& reason,
                                                 const std::string& body)
    : code_(code), reason_(reason), body_(body)
{
}

bool CRxUrlHandlerStaticJson::perform(http_req_head_para* req_head,
                                      std::string* recv_body,
                                      http_res_head_para* res_head,
                                      std::string* send_body,
                                      const ObjId& conn_id)
{
    (void)req_head;
    (void)recv_body;
    (void)conn_id;
    if (!res_head || !send_body) {
        return true;
    }

    res_head->_response_code = code_;
    res_head->_response_str = reason_;
    res_head->_headers["Content-Type"] = "application/json";

    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", body_.size());
    res_head->_headers["Content-Length"] = len_buf;
    res_head->_headers["Connection"] = "close";

    *send_body = body_;
    return true;
}

CRxUrlHandlerCaptureApi::CRxUrlHandlerCaptureApi()
{
}

bool CRxUrlHandlerCaptureApi::perform(http_req_head_para* req_head,
                                      std::string* recv_body,
                                      http_res_head_para* res_head,
                                      std::string* send_body,
                                      const ObjId& conn_id)
{
    if (!req_head) {
        if (res_head && send_body) {
            set_error_response(res_head, send_body, 500, "Internal error");
        }
        return true;
    }

    std::string path = req_head->_url_path;
    std::string method = req_head->_method;

    if (path.find("/api/capture/start") == 0 && method == "POST") {
        return handle_start(req_head, recv_body, res_head, send_body, conn_id);
    } else if (path.find("/api/capture/stop") == 0 && method == "POST") {
        return handle_stop(req_head, recv_body, res_head, send_body, conn_id);
    } else if (path.find("/api/capture/status") == 0 && method == "GET") {
        return handle_status(req_head, recv_body, res_head, send_body, conn_id);
    } else {
        set_error_response(res_head, send_body, 404, "Not found");
        return true;
    }
}

bool CRxUrlHandlerCaptureApi::handle_start(http_req_head_para* req_head,
                                           std::string* recv_body,
                                           http_res_head_para* res_head,
                                           std::string* send_body,
                                           const ObjId& conn_id)
{
    (void)req_head;

    rapidjson::Document doc;
    if (recv_body && !recv_body->empty()) {
        doc.Parse(recv_body->c_str());
        if (doc.HasParseError()) {
            set_error_response(res_head, send_body, 400, "Invalid JSON");
            return true;
        }
    }

    shared_ptr<SRxStartCaptureMsg> msg(new SRxStartCaptureMsg());

    if (doc.IsObject()) {

        if (doc.HasMember("capture_mode") && doc["capture_mode"].IsString()) {
            std::string mode = doc["capture_mode"].GetString();
            if (mode == "interface") msg->capture_mode = 0;
            else if (mode == "process") msg->capture_mode = 1;
            else if (mode == "pid") msg->capture_mode = 2;
            else if (mode == "container") msg->capture_mode = 3;
        } else if (doc.HasMember("mode") && doc["mode"].IsString()) {
            std::string mode = doc["mode"].GetString();
            if (mode == "interface") msg->capture_mode = 0;
            else if (mode == "process") msg->capture_mode = 1;
            else if (mode == "pid") msg->capture_mode = 2;
            else if (mode == "container") msg->capture_mode = 3;
        }

        if (doc.HasMember("iface") && doc["iface"].IsString()) {
            msg->iface = doc["iface"].GetString();
        }
        if (doc.HasMember("proc_name") && doc["proc_name"].IsString()) {
            msg->proc_name = doc["proc_name"].GetString();

            if (msg->capture_mode == 0 && !msg->proc_name.empty()) {
                msg->capture_mode = 1;
            }
        }
        if (doc.HasMember("pid") && doc["pid"].IsInt()) {
            msg->target_pid = doc["pid"].GetInt();
            if (msg->capture_mode == 0 && msg->target_pid > 0) {
                msg->capture_mode = 2;
            }
        }
        if (doc.HasMember("target_pid") && doc["target_pid"].IsInt()) {
            msg->target_pid = doc["target_pid"].GetInt();
        }
        if (doc.HasMember("container_id") && doc["container_id"].IsString()) {
            msg->container_id = doc["container_id"].GetString();
        }

        if (doc.HasMember("filter") && doc["filter"].IsString()) {
            msg->filter = doc["filter"].GetString();
        }
        if (doc.HasMember("protocol") && doc["protocol"].IsString()) {
            std::string protocol_name = doc["protocol"].GetString();

            /* Try to lookup protocol name in strategy config */
            CRxProcData* pdata = CRxProcData::instance();
            CRxStrategyConfigManager* cfg = pdata ? pdata->current_strategy_config() : NULL;
            if (cfg) {
                std::string pdef_path = cfg->get_protocol_pdef_path(protocol_name);
                if (!pdef_path.empty()) {
                    /* Found in config mapping */
                    msg->protocol_filter = pdef_path;
                } else {
                    /* Not found in config, try auto-discovery: config/protocols/<name>.pdef */
                    msg->protocol_filter = "config/protocols/" + protocol_name + ".pdef";
                }
            } else {
                /* Fallback: auto-discovery */
                msg->protocol_filter = "config/protocols/" + protocol_name + ".pdef";
            }
        }
        if (doc.HasMember("protocol_filter") && doc["protocol_filter"].IsString()) {
            msg->protocol_filter = doc["protocol_filter"].GetString();
        }
        if (doc.HasMember("protocol_filter_inline") && doc["protocol_filter_inline"].IsString()) {
            msg->protocol_filter_inline = doc["protocol_filter_inline"].GetString();
        }
        if (doc.HasMember("ip") && doc["ip"].IsString()) {
            msg->ip_filter = doc["ip"].GetString();
        }
        if (doc.HasMember("ip_filter") && doc["ip_filter"].IsString()) {
            msg->ip_filter = doc["ip_filter"].GetString();
        }
        if (doc.HasMember("port") && doc["port"].IsInt()) {
            msg->port_filter = doc["port"].GetInt();
        }
        if (doc.HasMember("port_filter") && doc["port_filter"].IsInt()) {
            msg->port_filter = doc["port_filter"].GetInt();
        }

        if (doc.HasMember("category") && doc["category"].IsString()) {
            msg->category = doc["category"].GetString();
        }
        if (doc.HasMember("file") && doc["file"].IsString()) {
            msg->file_pattern = doc["file"].GetString();
        }
        if (doc.HasMember("file_pattern") && doc["file_pattern"].IsString()) {
            msg->file_pattern = doc["file_pattern"].GetString();
        }
        if (doc.HasMember("duration") && doc["duration"].IsInt()) {
            msg->duration_sec = doc["duration"].GetInt();
        }
        if (doc.HasMember("max_bytes") && doc["max_bytes"].IsInt64()) {
            msg->max_bytes = doc["max_bytes"].GetInt64();
        }
        if (doc.HasMember("max_packets") && doc["max_packets"].IsInt()) {
            msg->max_packets = doc["max_packets"].GetInt();
        }

        if (doc.HasMember("client_ip") && doc["client_ip"].IsString()) {
            msg->client_ip = doc["client_ip"].GetString();
        }
        if (doc.HasMember("user") && doc["user"].IsString()) {
            msg->request_user = doc["user"].GetString();
        }
        if (doc.HasMember("request_user") && doc["request_user"].IsString()) {
            msg->request_user = doc["request_user"].GetString();
        }
    }

    msg->reply_target = conn_id;

    if (msg->capture_mode == 1 && !msg->proc_name.empty()) {
        std::vector<SProcessInfo> precheck = CRxProcessResolver::FindProcessesByName(msg->proc_name);
        if (precheck.empty()) {
            std::string body = std::string("{\"error\":\"process not found\",\"proc_name\":\"")
                + json_escape(msg->proc_name) + "\"}";
            set_json_response(res_head, send_body, 404, "Not Found", body);
            return true;
        }
    }

    uint64_t dispatch_begin = GetMilliSecond();
    bool handled = send_to_capture_manager(msg, res_head, send_body, conn_id);
    uint64_t dispatch_end = GetMilliSecond();
    LOG_NOTICE_MSG("HTTP start_capture dispatch result=%d elapsed_ms=%llu",
                   handled ? 1 : 0,
                   static_cast<unsigned long long>(dispatch_end - dispatch_begin));
    return handled;
}

bool CRxUrlHandlerCaptureApi::handle_stop(http_req_head_para* req_head,
                                          std::string* recv_body,
                                          http_res_head_para* res_head,
                                          std::string* send_body,
                                          const ObjId& conn_id)
{
    (void)recv_body;

    std::string path = req_head->_url_path;
    std::map<std::string, std::string> params = parse_query_params(path);

    int capture_id = 0;
    std::string sid;

    std::map<std::string, std::string>::const_iterator id_it = params.find("id");
    if (id_it != params.end()) {
        capture_id = std::atoi(id_it->second.c_str());
    }
    std::map<std::string, std::string>::const_iterator sid_it = params.find("sid");
    if (sid_it != params.end()) {
        sid = sid_it->second;
    }

    if (capture_id == 0 && sid.empty()) {
        set_error_response(res_head, send_body, 400, "Missing capture identifier");
        return true;
    }

    if (capture_id == 0 && !sid.empty()) {
        CRxProcData* pdata = CRxProcData::instance();
        if (!pdata) {
            set_error_response(res_head, send_body, 500, "Internal error");
            return true;
        }
        TaskSnapshot snapshot;
        if (!pdata->capture_task_mgr().query_task_by_sid(sid, snapshot)) {
            set_error_response(res_head, send_body, 404, "capture_not_found");
            return true;
        }
        capture_id = snapshot.capture_id;
    }

    if (capture_id == 0) {
        set_error_response(res_head, send_body, 400, "Missing capture identifier");
        return true;
    }

    shared_ptr<SRxStopCaptureMsg> msg(new SRxStopCaptureMsg());
    msg->capture_id = capture_id;

    msg->reply_target = conn_id;

    return send_to_capture_manager(msg, res_head, send_body, conn_id);
}

bool CRxUrlHandlerCaptureApi::handle_status(http_req_head_para* req_head,
                                            std::string* recv_body,
                                            http_res_head_para* res_head,
                                            std::string* send_body,
                                            const ObjId& conn_id)
{
    (void)recv_body;
    (void)conn_id;

    std::string path = req_head->_url_path;
    std::map<std::string, std::string> params = parse_query_params(path);

    int capture_id = 0;
    std::string sid;

    std::map<std::string, std::string>::const_iterator id_it = params.find("id");
    if (id_it != params.end()) {
        capture_id = std::atoi(id_it->second.c_str());
    }
    std::map<std::string, std::string>::const_iterator sid_it = params.find("sid");
    if (sid_it != params.end()) {
        sid = sid_it->second;
    }

    if (capture_id == 0 && sid.empty()) {
        set_error_response(res_head, send_body, 400, "Missing capture identifier");
        return true;
    }

    CRxProcData* pdata = CRxProcData::instance();
    if (!pdata) {
        set_error_response(res_head, send_body, 500, "Internal error");
        return true;
    }

    CRxSafeTaskMgr& task_mgr = pdata->capture_task_mgr();
    TaskSnapshot snapshot;
    if (capture_id > 0) {
        if (!task_mgr.query_task(capture_id, snapshot)) {
            set_error_response(res_head, send_body, 404, "capture_not_found");
            return true;
        }
    } else if (!sid.empty()) {
        if (!task_mgr.query_task_by_sid(sid, snapshot)) {
            set_error_response(res_head, send_body, 404, "capture_not_found");
            return true;
        }
        capture_id = snapshot.capture_id;
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"capture_id\":" << snapshot.capture_id;
    oss << ",\"status\":\"" << capture_status_to_string(snapshot.status) << "\"";
    oss << ",\"mode\":\"" << capture_mode_to_string(snapshot.capture_mode) << "\"";
    oss << ",\"key\":\"" << json_escape(snapshot.key) << "\"";
    if (!snapshot.sid.empty()) {
        oss << ",\"sid\":\"" << json_escape(snapshot.sid) << "\"";
    }

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
    oss << ",\"start_time\":" << snapshot.start_time;
    oss << ",\"end_time\":" << snapshot.end_time;
    oss << ",\"packets\":" << snapshot.packet_count;
    oss << ",\"bytes\":" << snapshot.bytes_captured;
    oss << ",\"worker\":" << snapshot.worker_thread_index;
    oss << ",\"stop_requested\":" << (snapshot.stop_requested ? "true" : "false");
    oss << ",\"client_ip\":\"" << json_escape(snapshot.client_ip) << "\"";
    oss << ",\"request_user\":\"" << json_escape(snapshot.request_user) << "\"";

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
                    const CaptureFileInfo& info = arc.files[j];
                    if (j > 0) {
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
            oss << "}";
        }
        oss << "]";
    }

    oss << "}";

    set_json_response(res_head, send_body, 200, "OK", oss.str());
    return true;
}

bool CRxUrlHandlerCaptureApi::send_to_capture_manager(shared_ptr<normal_msg> msg,
                                                      http_res_head_para* res_head,
                                                      std::string* send_body,
                                                      const ObjId& conn_id)
{
    if (conn_id._thread_index == 0) {
        set_error_response(res_head, send_body, 500, "Invalid connection id");
        return true;
    }

    CRxCaptureManagerThread* capture_thread = CRxProcData::instance()->get_capture_manager_thread();
    if (!capture_thread) {
        set_error_response(res_head, send_body, 500, "No owner thread");
        return true;
    }

    ObjId target;
    target._thread_index = capture_thread->get_thread_index();
    target._id = OBJ_ID_THREAD;

    uint64_t now_ms = GetMilliSecond();
    if (msg && msg->_msg_op == RX_MSG_START_CAPTURE) {
        shared_ptr<SRxStartCaptureMsg> start_msg =
            dynamic_pointer_cast<SRxStartCaptureMsg>(msg);
        if (start_msg) {
            start_msg->enqueue_ts_ms = now_ms;
            LOG_NOTICE_MSG("Enqueue start_capture request: conn=%u thread=%u mode=%d enqueue_ms=%llu",
                           conn_id._id,
                           target._thread_index,
                           start_msg->capture_mode,
                           static_cast<unsigned long long>(now_ms));
        }
    } else {
        LOG_NOTICE_MSG("Forwarding capture control msg op=%d to thread=%u enqueue_ms=%llu",
                       msg ? msg->_msg_op : -1,
                       target._thread_index,
                       static_cast<unsigned long long>(now_ms));
    }

    base_net_thread::put_obj_msg(target, msg);

    return false;
}

void CRxUrlHandlerCaptureApi::set_error_response(http_res_head_para* res_head,
                                                 std::string* send_body,
                                                 int code,
                                                 const std::string& message)
{
    std::string body = "{\"error\":\"" + json_escape(message) + "\"}";
    set_json_response(res_head, send_body, code, (code == 200) ? "OK" : "Error", body);
}

CRxUrlHandlerPdefUpload::CRxUrlHandlerPdefUpload()
{
}

static uint64_t fnv1a64(const char* data, size_t len)
{
    const uint64_t prime = 1099511628211ULL;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= prime;
    }
    return hash;
}

static void set_pdef_error(http_res_head_para* res_head,
                           std::string* send_body,
                           int code,
                           const std::string& message)
{
    std::string body = "{\"error\":\"" + json_escape(message) + "\"}";
    set_json_response(res_head, send_body, code, (code == 200) ? "OK" : "Error", body);
}

bool CRxUrlHandlerPdefUpload::perform(http_req_head_para* req_head,
                                      std::string* recv_body,
                                      http_res_head_para* res_head,
                                      std::string* send_body,
                                      const ObjId& conn_id)
{
    (void)conn_id;
    
    if (!req_head) {
        return true;
    }

    if (req_head->_method != "POST") {
        set_pdef_error(res_head, send_body, 405, "Only POST allowed");
        return true;
    }

    if (!recv_body || recv_body->empty()) {
        set_pdef_error(res_head, send_body, 400, "Empty body");
        return true;
    }

    static const size_t kMaxPdefSize = 2 * 1024 * 1024; /* 2MB safety cap */
    if (recv_body->size() > kMaxPdefSize) {
        set_pdef_error(res_head, send_body, 413, "PDEF too large");
        return true;
    }

    /* Validate PDEF by parsing before persisting */
    char err[512] = {0};
    ProtocolDef* proto = pdef_parse_string(recv_body->c_str(), err, sizeof(err));
    if (!proto) {
        std::string msg = "Invalid PDEF: ";
        msg += err;
        set_pdef_error(res_head, send_body, 400, msg);
        return true;
    }
    protocol_free(proto);

    /* Ensure temp directory exists */
    std::string base_dir = get_configured_pdef_dir();
    if (base_dir.empty()) {
        base_dir = "/tmp/rxtracenetcap_pdef";
    }
    struct stat st;
    if (stat(base_dir.c_str(), &st) != 0) {
        if (mkdir(base_dir.c_str(), 0700) != 0) {
            set_pdef_error(res_head, send_body, 500, "Failed to create temp dir");
            return true;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        set_pdef_error(res_head, send_body, 500, "Temp path exists and is not a directory");
        return true;
    }

    // Generate temp file path
    char temp_path[256];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // Use /tmp/rxtracenetcap_pdef/rxtracenetcap_pdef_TIMESTAMP_RANDOM.pdef
    unsigned int r = static_cast<unsigned int>(rand());
    snprintf(temp_path, sizeof(temp_path), "%s/rxtracenetcap_pdef_%lu_%06lu_%u.pdef",
             base_dir.c_str(), static_cast<unsigned long>(tv.tv_sec),
             static_cast<unsigned long>(tv.tv_usec), r);

    // Write body to file
    std::ofstream outfile(temp_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outfile.is_open()) {
        set_pdef_error(res_head, send_body, 500, "Failed to open temp file");
        return true;
    }

    outfile.write(recv_body->c_str(), recv_body->size());
    outfile.close();

    if (outfile.bad()) {
        set_pdef_error(res_head, send_body, 500, "Failed to write temp file");
        return true;
    }

    chmod(temp_path, S_IRUSR | S_IWUSR); /* 0600 */

    uint64_t checksum = fnv1a64(recv_body->c_str(), recv_body->size());

    // Return path JSON
    std::ostringstream oss;
    oss << "{\"status\":\"ok\",\"path\":\"" << json_escape(temp_path)
        << "\",\"size\":" << recv_body->size()
        << ",\"checksum\":\"" << std::hex << checksum << std::dec
        << "\",\"validated\":true}";
    set_json_response(res_head, send_body, 200, "OK", oss.str());

    LOG_NOTICE_MSG("Uploaded PDEF to %s (size=%zu, checksum=0x%llx)",
                   temp_path,
                   recv_body->size(),
                   static_cast<unsigned long long>(checksum));
    return true;
}

CRxUrlHandlerPdefManagement::CRxUrlHandlerPdefManagement()
{
}

bool CRxUrlHandlerPdefManagement::perform(http_req_head_para* req_head,
                                          std::string* recv_body,
                                          http_res_head_para* res_head,
                                          std::string* send_body,
                                          const ObjId& conn_id)
{
    if (!req_head) {
        set_error_response(res_head, send_body, 500, "Internal error");
        return true;
    }

    std::string path = req_head->_url_path;
    std::string method = req_head->_method;

    if (path.find("/api/pdef/list") == 0 && method == "GET") {
        return handle_list(req_head, recv_body, res_head, send_body, conn_id);
    } else if (path.find("/api/pdef/get") == 0 && method == "GET") {
        return handle_get(req_head, recv_body, res_head, send_body, conn_id);
    } else {
        set_error_response(res_head, send_body, 404, "Not found");
        return true;
    }
}

bool CRxUrlHandlerPdefManagement::handle_list(http_req_head_para* req_head,
                                               std::string* recv_body,
                                               http_res_head_para* res_head,
                                               std::string* send_body,
                                               const ObjId& conn_id)
{
    (void)req_head;
    (void)recv_body;
    (void)conn_id;

    std::vector<std::string> pdef_dirs;
    append_pdef_dirs(pdef_dirs);

    std::ostringstream oss;
    oss << "{\"status\":\"ok\",\"pdefs\":[";

    bool first = true;
    for (size_t d = 0; d < pdef_dirs.size(); ++d) {
        const std::string& dir = pdef_dirs[d];
        DIR* dp = opendir(dir.c_str());
        if (!dp) {
            continue;
        }

        struct dirent* entry;
        while ((entry = readdir(dp)) != NULL) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }

            size_t pdef_ext = name.rfind(".pdef");
            if (pdef_ext == std::string::npos || pdef_ext + 5 != name.size()) {
                continue;
            }

            std::string full_path = dir + "/" + name;

            struct stat st;
            if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }

            if (!first) {
                oss << ",";
            }
            first = false;

            oss << "{";
            oss << "\"name\":\"" << json_escape(name) << "\"";
            oss << ",\"path\":\"" << json_escape(full_path) << "\"";
            oss << ",\"size\":" << st.st_size;
            oss << ",\"mtime\":\"" << format_mtime(st.st_mtime) << "\"";
            oss << "}";
        }
        closedir(dp);
    }

    oss << "]}";

    set_json_response(res_head, send_body, 200, "OK", oss.str());
    return true;
}

bool CRxUrlHandlerPdefManagement::handle_get(http_req_head_para* req_head,
                                              std::string* recv_body,
                                              http_res_head_para* res_head,
                                              std::string* send_body,
                                              const ObjId& conn_id)
{
    (void)recv_body;
    (void)conn_id;

    std::string path = req_head->_url_path;
    std::map<std::string, std::string> params = parse_query_params(path);

    std::map<std::string, std::string>::const_iterator name_it = params.find("name");
    std::map<std::string, std::string>::const_iterator path_it = params.find("path");

    std::string target_path;

    if (path_it != params.end()) {
        target_path = path_it->second;
    } else if (name_it != params.end()) {
        std::string name = name_it->second;
        
        std::vector<std::string> search_dirs;
        append_pdef_dirs(search_dirs);

        for (size_t i = 0; i < search_dirs.size(); ++i) {
            std::string candidate = search_dirs[i] + "/" + name;
            struct stat st;
            if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                target_path = candidate;
                break;
            }
        }

        if (target_path.empty()) {
            set_error_response(res_head, send_body, 404, "PDEF not found");
            return true;
        }
    } else {
        set_error_response(res_head, send_body, 400, "Missing 'name' or 'path' parameter");
        return true;
    }

    if (!is_safe_path(target_path)) {
        set_error_response(res_head, send_body, 403, "Invalid path");
        return true;
    }

    struct stat st;
    if (stat(target_path.c_str(), &st) != 0) {
        set_error_response(res_head, send_body, 404, "File not found");
        return true;
    }

    if (!S_ISREG(st.st_mode)) {
        set_error_response(res_head, send_body, 400, "Not a regular file");
        return true;
    }

    static const size_t kMaxPdefSize = 2 * 1024 * 1024;
    if (static_cast<size_t>(st.st_size) > kMaxPdefSize) {
        set_error_response(res_head, send_body, 413, "File too large");
        return true;
    }

    std::ifstream file(target_path.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        set_error_response(res_head, send_body, 500, "Failed to open file");
        return true;
    }

    std::ostringstream content;
    content << file.rdbuf();
    file.close();

    if (file.bad()) {
        set_error_response(res_head, send_body, 500, "Failed to read file");
        return true;
    }

    std::string file_content = content.str();

    std::ostringstream json_oss;
    json_oss << "{";
    json_oss << "\"status\":\"ok\"";
    json_oss << ",\"path\":\"" << json_escape(target_path) << "\"";
    json_oss << ",\"size\":" << file_content.size();
    json_oss << ",\"mtime\":\"" << format_mtime(st.st_mtime) << "\"";
    json_oss << ",\"content\":\"" << json_escape(file_content) << "\"";
    json_oss << "}";

    set_json_response(res_head, send_body, 200, "OK", json_oss.str());
    return true;
}

void CRxUrlHandlerPdefManagement::set_json_response(http_res_head_para* res_head,
                                                     std::string* send_body,
                                                     int code,
                                                     const std::string& reason,
                                                     const std::string& json_body)
{
    if (!res_head || !send_body) {
        return;
    }
    res_head->_response_code = code;
    res_head->_response_str = reason;
    res_head->_headers.clear();
    res_head->_headers["Content-Type"] = "application/json";
    res_head->_headers["Connection"] = "close";
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%zu", json_body.size());
    res_head->_headers["Content-Length"] = len_buf;
    *send_body = json_body;
}

void CRxUrlHandlerPdefManagement::set_error_response(http_res_head_para* res_head,
                                                      std::string* send_body,
                                                      int code,
                                                      const std::string& message)
{
    std::string body = "{\"error\":\"" + json_escape(message) + "\"}";
    set_json_response(res_head, send_body, code, (code == 200) ? "OK" : "Error", body);
}

std::string CRxUrlHandlerPdefManagement::format_mtime(time_t mtime)
{
    char buf[32];
    struct tm tm_info;
    gmtime_r(&mtime, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    return std::string(buf);
}

bool CRxUrlHandlerPdefManagement::is_safe_path(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    if (path.find("..") != std::string::npos) {
        return false;
    }

    std::vector<std::string> allowed_dirs;
    append_pdef_dirs(allowed_dirs);

    for (size_t i = 0; i < allowed_dirs.size(); ++i) {
        std::string prefix = allowed_dirs[i];
        if (prefix.empty()) {
            continue;
        }
        if (prefix[prefix.size() - 1] != '/') {
            prefix.push_back('/');
        }
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }

    return false;
}

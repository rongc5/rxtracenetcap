#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#include "../third_party/rapidjson/document.h"
#include "../third_party/rapidjson/writer.h"
#include "../third_party/rapidjson/stringbuffer.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

struct TaskRequest {
    std::string payload;
    std::string summary;
};

struct HttpResponse {
    int status_code;
    std::string body;
    std::string raw;

    HttpResponse() : status_code(0) {}
};

void print_usage(const char* argv0)
{
    std::fprintf(stderr,
        "Usage: %s <tasks.json> [--host HOST] [--port PORT] [--path PATH]\n"
        "       Default host=127.0.0.1, port=8080, path=/api/capture/start\n",
        argv0);
}

bool read_file(const std::string& path, std::string* out, std::string* err)
{
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) {
        if (err) {
            *err = "failed to open file: " + path;
        }
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

std::string to_string_int64(long long value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", value);
    return std::string(buf);
}

bool parse_duration_value(const rapidjson::Value& value, long long* out, std::string* err)
{
    if (value.IsInt64()) {
        *out = value.GetInt64();
        return true;
    }
    if (value.IsInt()) {
        *out = value.GetInt();
        return true;
    }
    if (value.IsUint64()) {
        *out = static_cast<long long>(value.GetUint64());
        return true;
    }
    if (value.IsUint()) {
        *out = static_cast<long long>(value.GetUint());
        return true;
    }
    if (value.IsDouble()) {
        *out = static_cast<long long>(value.GetDouble());
        return true;
    }
    if (value.IsString()) {
        const char* s = value.GetString();
        if (!s || *s == '\0') {
            if (err) *err = "duration string cannot be empty";
            return false;
        }
        char* endptr = NULL;
        long long val = std::strtoll(s, &endptr, 10);
        if (!endptr || *endptr != '\0') {
            if (err) *err = "duration string is not a valid integer";
            return false;
        }
        *out = val;
        return true;
    }
    if (err) {
        *err = "duration must be an integer";
    }
    return false;
}

bool parse_duration(const rapidjson::Value& obj, long long* out, std::string* err)
{
    if (obj.HasMember("duration")) {
        if (parse_duration_value(obj["duration"], out, err)) {
            return true;
        }
        return false;
    }

    if (obj.HasMember("duration_sec")) {
        if (parse_duration_value(obj["duration_sec"], out, err)) {
            return true;
        }
        if (err && err->empty()) {
            *err = "duration_sec must be an integer";
        }
        return false;
    }

    return false;
}

bool add_pid_field(const rapidjson::Value& value,
                   const char* field_name,
                   rapidjson::Document& payload,
                   rapidjson::Document::AllocatorType& alloc,
                   std::string& summary,
                   std::string* err)
{
    long long pid_value = 0;
    if (value.IsInt64()) {
        pid_value = value.GetInt64();
    } else if (value.IsInt()) {
        pid_value = static_cast<long long>(value.GetInt());
    } else if (value.IsUint64()) {
        pid_value = static_cast<long long>(value.GetUint64());
    } else if (value.IsUint()) {
        pid_value = static_cast<long long>(value.GetUint());
    } else {
        if (err) {
            *err = std::string(field_name) + " must be an integer";
        }
        return false;
    }

    rapidjson::Value pid_json;
    pid_json.SetInt64(pid_value);
    rapidjson::Value name;
    name.SetString(field_name, static_cast<rapidjson::SizeType>(std::strlen(field_name)), alloc);
    payload.AddMember(name, pid_json, alloc);

    if (!summary.empty()) {
        summary += " ";
    }
    const char* summary_label = (std::strcmp(field_name, "target_pid") == 0)
        ? "target_pid"
        : "pid";
    summary += summary_label;
    summary += "=";
    summary += to_string_int64(pid_value);
    return true;
}

bool build_task_payload(const rapidjson::Value& item, TaskRequest* task, std::string* err)
{
    if (!item.IsObject()) {
        if (err) {
            *err = "task entry must be a JSON object";
        }
        return false;
    }

    rapidjson::Document payload;
    payload.SetObject();
    rapidjson::Document::AllocatorType& alloc = payload.GetAllocator();

    std::string summary;

    if (item.HasMember("iface")) {
        if (!item["iface"].IsString()) {
            if (err) *err = "iface must be a string";
            return false;
        }
        payload.AddMember("iface", rapidjson::Value(item["iface"].GetString(), alloc).Move(), alloc);
        summary += "iface=";
        summary += item["iface"].GetString();
    }

    long long duration_value = 0;
    if (parse_duration(item, &duration_value, err)) {
        rapidjson::Value duration_json;
        duration_json.SetInt64(duration_value);
        payload.AddMember("duration", duration_json, alloc);
        if (!summary.empty()) summary += " ";
        summary += "duration=" + to_string_int64(duration_value);
    }

    if (item.HasMember("filter")) {
        if (!item["filter"].IsString()) {
            if (err) *err = "filter must be a string";
            return false;
        }
        payload.AddMember("filter", rapidjson::Value(item["filter"].GetString(), alloc).Move(), alloc);
        if (!summary.empty()) summary += " ";
        summary += "filter=";
        summary += item["filter"].GetString();
    } else if (item.HasMember("bpf")) {
        if (!item["bpf"].IsString()) {
            if (err) *err = "bpf must be a string";
            return false;
        }
        payload.AddMember("filter", rapidjson::Value(item["bpf"].GetString(), alloc).Move(), alloc);
        if (!summary.empty()) summary += " ";
        summary += "filter=";
        summary += item["bpf"].GetString();
    }

    bool has_output_override = false;
    if (item.HasMember("file")) {
        if (!item["file"].IsString()) {
            if (err) *err = "file must be a string";
            return false;
        }
        payload.AddMember("file", rapidjson::Value(item["file"].GetString(), alloc).Move(), alloc);
        if (!summary.empty()) summary += " ";
        summary += "file=";
        summary += item["file"].GetString();
        has_output_override = true;
    }
    if (item.HasMember("file_pattern")) {
        if (!item["file_pattern"].IsString()) {
            if (err) *err = "file_pattern must be a string";
            return false;
        }
        payload.AddMember("file_pattern", rapidjson::Value(item["file_pattern"].GetString(), alloc).Move(), alloc);
        if (!summary.empty()) summary += " ";
        summary += "file_pattern=";
        summary += item["file_pattern"].GetString();
        has_output_override = true;
    }
    if (!has_output_override) {
        if (!summary.empty()) summary += " ";
        summary += "file=<default>";
    }

    if (item.HasMember("category")) {
        if (!item["category"].IsString()) {
            if (err) *err = "category must be a string";
            return false;
        }
        payload.AddMember("category", rapidjson::Value(item["category"].GetString(), alloc).Move(), alloc);
    }

    if (item.HasMember("priority")) {
        if (!item["priority"].IsString()) {
            if (err) *err = "priority must be a string";
            return false;
        }
        payload.AddMember("priority", rapidjson::Value(item["priority"].GetString(), alloc).Move(), alloc);
    }

    if (item.HasMember("proc_name")) {
        if (!item["proc_name"].IsString()) {
            if (err) *err = "proc_name must be a string";
            return false;
        }
        payload.AddMember("proc_name", rapidjson::Value(item["proc_name"].GetString(), alloc).Move(), alloc);
        if (!summary.empty()) summary += " ";
        summary += "proc=";
        summary += item["proc_name"].GetString();
    }

    if (item.HasMember("pid")) {
        if (!add_pid_field(item["pid"], "pid", payload, alloc, summary, err)) {
            return false;
        }
    } else if (item.HasMember("target_pid")) {
        if (!add_pid_field(item["target_pid"], "target_pid", payload, alloc, summary, err)) {
            return false;
        }
    }

    if (item.HasMember("protocol")) {
        if (!item["protocol"].IsString()) {
            if (err) *err = "protocol must be a string";
            return false;
        }
        payload.AddMember("protocol", rapidjson::Value(item["protocol"].GetString(), alloc).Move(), alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    payload.Accept(writer);

    task->payload = buffer.GetString();
    task->summary = summary;
    return true;
}

bool parse_tasks(const std::string& json, std::vector<TaskRequest>* out, std::string* err)
{
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError()) {
        if (err) {
            *err = "failed to parse tasks JSON (offset " + to_string_int64(static_cast<long long>(doc.GetErrorOffset())) + ")";
        }
        return false;
    }
    if (!doc.IsArray()) {
        if (err) *err = "tasks JSON must be an array";
        return false;
    }

    for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
        TaskRequest task;
        std::string task_err;
        if (!build_task_payload(doc[i], &task, &task_err)) {
            if (err) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "task index %u: %s", static_cast<unsigned>(i), task_err.c_str());
                *err = buf;
            }
            return false;
        }
        out->push_back(task);
    }
    return true;
}

bool send_all(int sockfd, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(sockfd, data + sent, len - sent, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_all(int sockfd, std::string* out)
{
    char buf[4096];
    size_t header_end_pos = std::string::npos;
    long long content_length = -1;
    size_t expected_total = std::string::npos;

    while (true) {
        ssize_t n = ::recv(sockfd, buf, sizeof(buf), 0);
        if (n > 0) {
            out->append(buf, static_cast<size_t>(n));
            if (header_end_pos == std::string::npos) {
                size_t pos = out->find("\r\n\r\n");
                if (pos != std::string::npos) {
                    header_end_pos = pos + 4;
                    
                    size_t cl_pos = out->find("Content-Length:", 0);
                    if (cl_pos != std::string::npos && cl_pos < header_end_pos) {
                        size_t cl_end_line = out->find("\r\n", cl_pos);
                        size_t value_start = cl_pos + strlen("Content-Length:");
                        while (value_start < out->size() && out->at(value_start) == ' ') {
                            ++value_start;
                        }
                        if (cl_end_line != std::string::npos && cl_end_line <= header_end_pos) {
                            std::string len_str = out->substr(value_start, cl_end_line - value_start);
                            content_length = std::strtoll(len_str.c_str(), NULL, 10);
                            if (content_length >= 0) {
                                expected_total = header_end_pos + static_cast<size_t>(content_length);
                            }
                        }
                    }
                }
            }
            if (expected_total != std::string::npos && out->size() >= expected_total) {
                return true;
            }
            continue;
        }
        if (n == 0) {
            return true; 
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

bool send_http_post(const std::string& host,
                    int port,
                    const std::string& path,
                    const std::string& payload,
                    HttpResponse* resp,
                    std::string* err)
{
    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int gai = ::getaddrinfo(host.c_str(), port_str, &hints, &res);
    if (gai != 0) {
        if (err) {
            *err = "getaddrinfo failed: ";
            *err += ::gai_strerror(gai);
        }
        return false;
    }

    int sockfd = -1;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        sockfd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        if (::connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; 
        }
        ::close(sockfd);
        sockfd = -1;
    }
    ::freeaddrinfo(res);

    if (sockfd < 0) {
        if (err) *err = "failed to connect to host";
        return false;
    }

    std::string normalized_path = path;
    if (normalized_path.empty()) {
        normalized_path = "/";
    } else if (normalized_path[0] != '/') {
        normalized_path.insert(normalized_path.begin(), '/');
    }

    std::string request;
    request.reserve(payload.size() + 256);
    request += "POST ";
    request += normalized_path;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += host;
    request += ':';
    request += port_str;
    request += "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Connection: close\r\n";
    char len_buf[64];
    std::snprintf(len_buf, sizeof(len_buf), "%zu", payload.size());
    request += "Content-Length: ";
    request += len_buf;
    request += "\r\n\r\n";
    request += payload;

    bool ok = send_all(sockfd, request.c_str(), request.size());
    if (!ok) {
        if (err) *err = "failed to send HTTP request";
        ::close(sockfd);
        return false;
    }

    std::string response;
    ok = recv_all(sockfd, &response);
    ::close(sockfd);
    if (!ok) {
        if (err) *err = "failed to read HTTP response";
        return false;
    }

    resp->raw = response;

    size_t status_end = response.find("\r\n");
    if (status_end == std::string::npos) {
        if (err) *err = "malformed HTTP response";
        return false;
    }
    std::string status_line = response.substr(0, status_end);

    const std::string prefix = "HTTP/";
    if (status_line.compare(0, prefix.size(), prefix) != 0) {
        if (err) *err = "unexpected HTTP status line";
        return false;
    }
    size_t first_space = status_line.find(' ');
    if (first_space == std::string::npos) {
        if (err) *err = "unexpected HTTP status line";
        return false;
    }
    size_t second_space = status_line.find(' ', first_space + 1);
    std::string code_str;
    if (second_space == std::string::npos) {
        code_str = status_line.substr(first_space + 1);
    } else {
        code_str = status_line.substr(first_space + 1, second_space - first_space - 1);
    }
    resp->status_code = std::atoi(code_str.c_str());

    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (err) *err = "malformed HTTP headers";
        return false;
    }
    resp->body = response.substr(header_end + 4);
    return true;
}

void summarize_response(const HttpResponse& resp)
{
    rapidjson::Document doc;
    doc.Parse(resp.body.c_str());
    if (!doc.HasParseError() && doc.IsObject()) {
        if (doc.HasMember("capture_id") && doc["capture_id"].IsInt()) {
            std::printf("    capture_id: %d\n", doc["capture_id"].GetInt());
            if (doc.HasMember("key") && doc["key"].IsString()) {
                std::printf("    key: %s\n", doc["key"].GetString());
            }
            if (doc.HasMember("sid") && doc["sid"].IsString()) {
                std::printf("    sid: %s\n", doc["sid"].GetString());
            }
            if (doc.HasMember("status") && doc["status"].IsString()) {
                std::printf("    status: %s\n", doc["status"].GetString());
            }
            if (doc.HasMember("path") && doc["path"].IsString()) {
                std::printf("    path: %s\n", doc["path"].GetString());
            }
            if (doc.HasMember("duplicate") && doc["duplicate"].IsBool()) {
                std::printf("    duplicate: %s\n", doc["duplicate"].GetBool() ? "true" : "false");
            }
        } else if (doc.HasMember("id") && doc["id"].IsInt()) {
            std::printf("    id: %d\n", doc["id"].GetInt());
        } else {
            std::printf("    response: %s\n", resp.body.c_str());
        }
    } else if (!resp.body.empty()) {
        std::printf("    response: %s\n", resp.body.c_str());
    }
}

} 

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/api/capture/start";
    std::string tasks_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (tasks_path.empty()) {
            tasks_path = argv[i];
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (tasks_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::string json;
    std::string err;
    if (!read_file(tasks_path, &json, &err)) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }

    std::vector<TaskRequest> tasks;
    if (!parse_tasks(json, &tasks, &err)) {
        std::fprintf(stderr, "Error: %s\n", err.c_str());
        return 1;
    }

    if (tasks.empty()) {
        std::fprintf(stderr, "No tasks found in %s\n", tasks_path.c_str());
        return 1;
    }

    std::printf("Submitting %zu capture task(s) to %s:%d%s\n",
                tasks.size(), host.c_str(), port, path.c_str());

    bool success = true;
    for (size_t i = 0; i < tasks.size(); ++i) {
        const TaskRequest& task = tasks[i];
        std::printf("Task %zu: %s\n", i + 1, task.summary.empty() ? "(no summary)" : task.summary.c_str());

        HttpResponse resp;
        std::string send_err;
        if (!send_http_post(host, port, path, task.payload, &resp, &send_err)) {
            std::printf("    error: %s\n", send_err.c_str());
            success = false;
            continue;
        }

        std::printf("    HTTP %d\n", resp.status_code);
        summarize_response(resp);

        if (resp.status_code >= 400) {
            success = false;
        }
    }

    return success ? 0 : 1;
}

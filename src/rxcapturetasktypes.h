#ifndef RXTRACENETCAP_CAPTURE_TASK_TYPES_H
#define RXTRACENETCAP_CAPTURE_TASK_TYPES_H

#include "legacy_core.h"
#include <string>
#include <vector>
#include <map>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;
#include <sys/types.h>
#include <stdint.h>

struct CaptureFileInfo {
    std::string file_path;
    unsigned long file_size;
    int segment_index;
    int total_segments;
    std::string md5;
    int64_t file_ready_ts;
    bool compressed;
    std::string archive_path;
    int64_t compress_finish_ts;
    std::string record_path;

    CaptureFileInfo()
        : file_size(0)
        , segment_index(0)
        , total_segments(1)
        , file_ready_ts(0)
        , compressed(false)
        , compress_finish_ts(0)
    {
    }
};

struct CaptureArchiveInfo {
    std::string archive_path;
    std::vector<CaptureFileInfo> files;
    unsigned long archive_size;
    int64_t compress_finish_ts;

    CaptureArchiveInfo()
        : archive_size(0)
        , compress_finish_ts(0)
    {
    }
};

enum ECaptureMode {
    MODE_INTERFACE = 0,
    MODE_PROCESS = 1,
    MODE_PID = 2,
    MODE_CONTAINER = 3
};

enum ECaptureTaskStatus {
    STATUS_PENDING = 0,
    STATUS_RESOLVING = 1,
    STATUS_RUNNING = 2,
    STATUS_COMPLETED = 3,
    STATUS_FAILED = 4,
    STATUS_STOPPED = 5
};

struct SRxCaptureTask {

    int capture_id;
    std::string key;
    std::string signature;
    std::string sid;

    ECaptureMode capture_mode;
    std::string iface;
    std::string proc_name;
    pid_t target_pid;
    std::string container_id;
    std::string netns_path;

    std::string filter;
    std::string protocol_filter;
    std::string ip_filter;
    int port_filter;

    std::string category;
    std::string file_pattern;
    int duration_sec;
    long max_bytes;
    int max_packets;
    int priority;

    ECaptureTaskStatus status;

    pid_t capture_pid;
    std::string output_file;
    long start_time;
    long end_time;

    std::vector<pid_t> matched_pids;
    std::string resolved_iface;
    uint32_t worker_thread_index;
    bool stop_requested;
    bool cancel_requested;

    unsigned long packet_count;
   unsigned long bytes_captured;
   std::string error_message;

    std::vector<CaptureFileInfo> captured_files;
    std::vector<CaptureArchiveInfo> archives;

    ObjId reply_target;
    std::string client_ip;
    std::string request_user;
    SRxCaptureTask();
};

struct CaptureTaskTable {
    std::map<std::string, shared_ptr<SRxCaptureTask> > tasks;
    std::map<int, std::string> id_to_key;

    void add_task(const std::string& key, const shared_ptr<SRxCaptureTask>& task) {
        tasks[key] = task;
        id_to_key[task->capture_id] = key;
    }

    void update_status(int capture_id, ECaptureTaskStatus new_status) {
        std::map<int, std::string>::iterator id_it = id_to_key.find(capture_id);
        if (id_it == id_to_key.end()) {
            return;
        }

        const std::string& key = id_it->second;
            std::map<std::string, shared_ptr<SRxCaptureTask> >::iterator it = tasks.find(key);
            if (it != tasks.end() && it->second) {
                shared_ptr<SRxCaptureTask> new_task(new SRxCaptureTask(*(it->second)));
                new_task->status = new_status;
                tasks[key] = new_task;
            }
    }

    void remove_task(int capture_id) {
        std::map<int, std::string>::iterator id_it = id_to_key.find(capture_id);
        if (id_it != id_to_key.end()) {
            const std::string& key = id_it->second;
            tasks.erase(key);
            id_to_key.erase(id_it);
        }
    }
};

#endif

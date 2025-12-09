#ifndef RXCAPTUREMESSAGES_H
#define RXCAPTUREMESSAGES_H

#include "rxmsgtypes.h"
#include "legacy_core.h"
#include "rxcapturetasktypes.h"

#include <map>
#include <string>
#include <vector>
#include <sys/time.h>
#include <stdint.h>

inline int64_t rx_capture_now_usec()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

enum ECaptureErrorCode {
    ERR_NONE = 0,
    ERR_UNKNOWN = 1,

    ERR_START_INVALID_PARAMS = 100,
    ERR_START_NO_PERMISSION = 101,
    ERR_START_INTERFACE_NOT_FOUND = 102,
    ERR_START_PROCESS_NOT_FOUND = 103,
    ERR_START_TCPDUMP_FAILED = 104,
    ERR_START_CREATE_FILE_FAILED = 105,

    ERR_RUN_TCPDUMP_DIED = 200,
    ERR_RUN_DISK_FULL = 201,
    ERR_RUN_TIMEOUT = 202,
    ERR_RUN_CANCELLED = 203,
    ERR_RUN_PROCESS_DIED = 204,

    ERR_CLEAN_COMPRESS_FAILED = 300,
    ERR_CLEAN_DELETE_FAILED = 301,
    ERR_CLEAN_DISK_FULL = 302
};

inline const char* rx_capture_error_to_string(ECaptureErrorCode code)
{
    switch (code) {
        case ERR_NONE: return "ERR_NONE";
        case ERR_UNKNOWN: return "ERR_UNKNOWN";
        case ERR_START_INVALID_PARAMS: return "ERR_START_INVALID_PARAMS";
        case ERR_START_NO_PERMISSION: return "ERR_START_NO_PERMISSION";
        case ERR_START_INTERFACE_NOT_FOUND: return "ERR_START_INTERFACE_NOT_FOUND";
        case ERR_START_PROCESS_NOT_FOUND: return "ERR_START_PROCESS_NOT_FOUND";
        case ERR_START_TCPDUMP_FAILED: return "ERR_START_TCPDUMP_FAILED";
        case ERR_START_CREATE_FILE_FAILED: return "ERR_START_CREATE_FILE_FAILED";
        case ERR_RUN_TCPDUMP_DIED: return "ERR_RUN_TCPDUMP_DIED";
        case ERR_RUN_DISK_FULL: return "ERR_RUN_DISK_FULL";
        case ERR_RUN_TIMEOUT: return "ERR_RUN_TIMEOUT";
        case ERR_RUN_CANCELLED: return "ERR_RUN_CANCELLED";
        case ERR_RUN_PROCESS_DIED: return "ERR_RUN_PROCESS_DIED";
        case ERR_CLEAN_COMPRESS_FAILED: return "ERR_CLEAN_COMPRESS_FAILED";
        case ERR_CLEAN_DELETE_FAILED: return "ERR_CLEAN_DELETE_FAILED";
        case ERR_CLEAN_DISK_FULL: return "ERR_CLEAN_DISK_FULL";
        default: return "ERR_UNKNOWN";
    }
}

struct CaptureConfigSnapshot {

    std::string output_dir;
    std::string filename_template;
    int file_rotate_size_mb;
    int file_rotate_count;

    int max_duration_sec;
    long max_bytes;
    int max_packets;
    int snaplen;

    bool compress_enabled;
    int compress_threshold_mb;
    std::string compress_format;
    int compress_level;
    bool compress_remove_src;

    int retain_days;
    int clean_batch_size;
    long disk_threshold_gb;

    int progress_interval_sec;
    int progress_packet_threshold;
    long progress_bytes_threshold;

    uint32_t config_hash;
    int64_t config_timestamp;

    CaptureConfigSnapshot()
        : file_rotate_size_mb(100)
        , file_rotate_count(0)
        , max_duration_sec(0)
        , max_bytes(0)
        , max_packets(0)
        , snaplen(65535)
        , compress_enabled(true)
        , compress_threshold_mb(100)
        , compress_format("tar.gz")
        , compress_level(6)
        , compress_remove_src(false)
        , retain_days(7)
        , clean_batch_size(100)
        , disk_threshold_gb(10)
        , progress_interval_sec(2)
        , progress_packet_threshold(10000)
        , progress_bytes_threshold(100 * 1024 * 1024L)
        , config_hash(0)
        , config_timestamp(0)
    {
    }
};

struct CaptureSpec {
    ECaptureMode capture_mode;
    std::string iface;
    std::string proc_name;
    pid_t target_pid;
    std::string container_id;
    std::string netns_path;
    std::string category;

    std::string filter;
    std::string protocol_filter;           // Path to .pdef file
    std::string protocol_filter_inline;    // Inline PDEF content
    std::string ip_filter;
    int port_filter;

    std::string output_pattern;
    std::string resolved_iface;

    int max_duration_sec;
    long max_bytes;
    int max_packets;
    int snaplen;

    CaptureSpec()
        : capture_mode(MODE_INTERFACE)
        , target_pid(-1)
        , port_filter(0)
        , max_duration_sec(0)
        , max_bytes(0)
        , max_packets(0)
        , snaplen(65535)
    {
    }
};

struct CaptureProgressStats {
    unsigned long packets;
    unsigned long bytes;
    int64_t first_packet_ts;
    int64_t last_packet_ts;
    unsigned long file_size;
    double cpu_seconds;

    CaptureProgressStats()
        : packets(0)
        , bytes(0)
        , first_packet_ts(0)
        , last_packet_ts(0)
        , file_size(0)
        , cpu_seconds(0.0)
    {
    }
};

struct CaptureResultStats {
    unsigned long total_packets;
    unsigned long total_bytes;
    int64_t start_ts;
    int64_t finish_ts;
    int exit_code;
    std::string error_message;

    CaptureResultStats()
        : total_packets(0)
        , total_bytes(0)
        , start_ts(0)
        , finish_ts(0)
        , exit_code(0)
    {
    }
};

struct CaptureMessageBase : public normal_msg {
    int capture_id;
    std::string key;
    std::string sid;
    int op_version;
    int64_t ts_usec;
    uint32_t config_hash;
    int sender_thread_index;

    CaptureMessageBase(int op_code)
        : normal_msg(op_code)
        , capture_id(-1)
        , op_version(1)
        , ts_usec(rx_capture_now_usec())
        , config_hash(0)
        , sender_thread_index(0)
    {
    }
};

struct SRxCaptureStartMsgV2 : public CaptureMessageBase {
    CaptureConfigSnapshot config;
    CaptureSpec spec;
    int worker_id;

    SRxCaptureStartMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_START)
        , worker_id(0)
    {
    }
};

struct SRxCaptureStopMsgV2 : public CaptureMessageBase {
    std::string stop_reason;

    SRxCaptureStopMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_STOP)
    {
    }
};

struct SRxCaptureCancelMsgV2 : public CaptureMessageBase {
    std::string cancel_reason;
    ECaptureErrorCode error_code;

    SRxCaptureCancelMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_CANCEL)
        , error_code(ERR_UNKNOWN)
    {
    }
};

struct SRxCaptureConfigRefreshMsgV2 : public CaptureMessageBase {
    CaptureConfigSnapshot config;

    SRxCaptureConfigRefreshMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_CONFIG_REFRESH)
    {
    }
};

struct SRxCaptureStartedMsgV2 : public CaptureMessageBase {
    int64_t start_ts;
    pid_t capture_pid;
    std::string output_file;

    SRxCaptureStartedMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_STARTED)
        , start_ts(0)
        , capture_pid(-1)
    {
    }
};

struct SRxCaptureProgressMsgV2 : public CaptureMessageBase {
    CaptureProgressStats progress;
    int64_t report_ts;

    SRxCaptureProgressMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_PROGRESS)
        , report_ts(rx_capture_now_usec())
    {
    }
};

struct SRxCaptureFileReadyMsgV2 : public CaptureMessageBase {
    std::vector<CaptureFileInfo> files;

    SRxCaptureFileReadyMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_FILE_READY)
    {
    }
};

struct SRxCaptureFinishedMsgV2 : public CaptureMessageBase {
    CaptureResultStats result;

    SRxCaptureFinishedMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_FINISHED)
    {
    }
};

struct SRxCaptureFailedMsgV2 : public CaptureMessageBase {
    ECaptureErrorCode error_code;
    std::string error_message;
    CaptureProgressStats last_progress;

    SRxCaptureFailedMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_FAILED)
        , error_code(ERR_UNKNOWN)
    {
    }
};

struct SRxCaptureHeartbeatMsgV2 : public CaptureMessageBase {
    int64_t last_progress_ts;

    SRxCaptureHeartbeatMsgV2()
        : CaptureMessageBase(RX_MSG_CAPTURE_HEARTBEAT)
        , last_progress_ts(0)
    {
    }
};

struct SRxFileEnqueueMsgV2 : public CaptureMessageBase {
    std::vector<CaptureFileInfo> files;
    CaptureConfigSnapshot clean_policy;

    SRxFileEnqueueMsgV2()
        : CaptureMessageBase(RX_MSG_FILE_ENQUEUE)
    {
    }
};

struct SRxCleanConfigRefreshMsgV2 : public CaptureMessageBase {
    CaptureConfigSnapshot clean_policy;

    SRxCleanConfigRefreshMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_CFG_REFRESH)
    {
    }
};

struct SRxCleanShutdownMsgV2 : public CaptureMessageBase {
    SRxCleanShutdownMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_SHUTDOWN)
    {
    }
};

struct SRxCleanStoredMsgV2 : public CaptureMessageBase {
    CaptureFileInfo stored_file;
   size_t pending_count;
   unsigned long pending_bytes;

    SRxCleanStoredMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_STORED)
        , pending_count(0)
        , pending_bytes(0)
    {
    }
};

struct SRxCleanCompressDoneMsgV2 : public CaptureMessageBase {
    std::vector<CaptureFileInfo> compressed_files;
    std::string archive_path;
    unsigned long compressed_bytes;
    int64_t compress_duration_ms;

    SRxCleanCompressDoneMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_COMPRESS_DONE)
        , compressed_bytes(0)
        , compress_duration_ms(0)
    {
    }
};

struct SRxCleanCompressFailedMsgV2 : public CaptureMessageBase {
    std::vector<CaptureFileInfo> failed_files;
    ECaptureErrorCode error_code;
    std::string error_message;

    SRxCleanCompressFailedMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_COMPRESS_FAILED)
        , error_code(ERR_CLEAN_COMPRESS_FAILED)
    {
    }
};

struct SRxCleanHeartbeatMsgV2 : public CaptureMessageBase {
    size_t queue_size;
    unsigned long queue_bytes;

    SRxCleanHeartbeatMsgV2()
        : CaptureMessageBase(RX_MSG_CLEAN_HEARTBEAT)
        , queue_size(0)
        , queue_bytes(0)
    {
    }
};

struct SRxStartCaptureMsg : public normal_msg {
    int capture_mode;
    std::string iface;
    std::string proc_name;
    int target_pid;
    std::string container_id;
    std::string netns_path;
    std::string filter;
    std::string protocol_filter;           // Path to .pdef file
    std::string protocol_filter_inline;    // Inline PDEF content
    std::string ip_filter;
    int port_filter;
    std::string category;
    std::string file_pattern;
    int duration_sec;
    long max_bytes;
    int max_packets;
    ObjId reply_target;
    std::string client_ip;
    std::string request_user;
    uint64_t enqueue_ts_ms;
    std::string sid;

    SRxStartCaptureMsg()
        : normal_msg(RX_MSG_START_CAPTURE)
        , capture_mode(0)
        , target_pid(-1)
        , port_filter(0)
        , duration_sec(60)
        , max_bytes(0)
        , max_packets(0)
        , enqueue_ts_ms(0)
    {
    }
};

struct SRxStopCaptureMsg : public normal_msg {
    int capture_id;
    ObjId reply_target;

    SRxStopCaptureMsg()
        : normal_msg(RX_MSG_STOP_CAPTURE)
        , capture_id(0)
    {
    }
};

struct SRxQueryCaptureMsg : public normal_msg {
    int capture_id;
    ObjId reply_target;

    SRxQueryCaptureMsg()
        : normal_msg(RX_MSG_QUERY_CAPTURE)
        , capture_id(0)
    {
    }
};

struct SRxTaskUpdateMsg : public normal_msg {
    int capture_id;
    ECaptureTaskStatus new_status;
    bool update_capture_pid;
    int capture_pid;
    bool update_output_file;
    std::string output_file;
    bool update_start_time;
    long start_time;
    bool update_end_time;
    long end_time;
    bool update_stats;
    unsigned long packet_count;
    unsigned long bytes_captured;
    bool update_error;
    std::string error_message;

    SRxTaskUpdateMsg()
        : normal_msg(RX_MSG_TASK_UPDATE)
        , capture_id(-1)
        , new_status(STATUS_PENDING)
        , update_capture_pid(false)
        , capture_pid(-1)
        , update_output_file(false)
        , update_start_time(false)
        , start_time(0)
        , update_end_time(false)
        , end_time(0)
        , update_stats(false)
        , packet_count(0)
        , bytes_captured(0)
        , update_error(false)
    {
    }
};

struct SRxCaptureStartedMsg : public normal_msg {
    int capture_id;
    long start_timestamp;

    SRxCaptureStartedMsg()
        : normal_msg(RX_MSG_CAP_STARTED)
        , capture_id(0)
        , start_timestamp(0)
    {
    }
};

struct SRxCaptureFinishedMsg : public normal_msg {
    int capture_id;
    int exit_code;
    unsigned long packet_count;
    std::string final_filepath;
    long finish_timestamp;

    SRxCaptureFinishedMsg()
        : normal_msg(RX_MSG_CAP_FINISHED)
        , capture_id(0)
        , exit_code(0)
        , packet_count(0)
        , finish_timestamp(0)
    {
    }
};

#endif

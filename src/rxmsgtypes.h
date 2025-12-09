#ifndef RX_MSG_TYPES_H
#define RX_MSG_TYPES_H

#include <map>
#include <string>
#include <stdint.h>
#include "legacy_core.h"

#define RX_MSG_NONE 0

struct SRxHttpReplyMsg : public normal_msg {
    int status;
    std::string reason;
    std::map<std::string, std::string> headers;
    std::string body;
    uint32_t conn_id;
    uint64_t debug_request_ts_ms;
    uint64_t debug_reply_ts_ms;

    SRxHttpReplyMsg()
        : normal_msg(NORMAL_MSG_HTTP_REPLY), status(200), conn_id(0),
          debug_request_ts_ms(0), debug_reply_ts_ms(0)
    {
    }
};

enum ERxHttpMsg {
    RX_MSG_HTTP_REPLY = 1003
};

enum ERxCaptureMsg {
    RX_MSG_START_CAPTURE = 2001,
    RX_MSG_STOP_CAPTURE = 2002,
    RX_MSG_QUERY_CAPTURE = 2003,
    RX_MSG_CLEAN_EXPIRED = 2004,
    RX_MSG_COMPRESS_FILES = 2005,
    RX_MSG_CHECK_THRESHOLD = 2006,
    RX_MSG_TASK_UPDATE = 2007,

    RX_MSG_CAPTURE_START = 20000,
    RX_MSG_CAPTURE_STOP = 20001,
    RX_MSG_CAPTURE_CANCEL = 20002,
    RX_MSG_CAPTURE_CONFIG_REFRESH = 20003,

    RX_MSG_CAPTURE_STARTED = 30000,
    RX_MSG_CAPTURE_PROGRESS = 30001,
    RX_MSG_CAPTURE_FILE_READY = 30002,
    RX_MSG_CAPTURE_FINISHED = 30003,
    RX_MSG_CAPTURE_FAILED = 30004,
    RX_MSG_CAPTURE_HEARTBEAT = 30005,

    RX_MSG_FILE_ENQUEUE = 40000,
    RX_MSG_CLEAN_CFG_REFRESH = 40001,
    RX_MSG_CLEAN_SHUTDOWN = 40002,

    RX_MSG_CLEAN_STORED = 50000,
    RX_MSG_CLEAN_COMPRESS_DONE = 50001,
    RX_MSG_CLEAN_COMPRESS_FAILED = 50002,
    RX_MSG_CLEAN_HEARTBEAT = 50003
};

enum ERxSampleMsg {
    RX_MSG_SAMPLE_TRIGGER = 3001,
    RX_MSG_SAMPLE_ALERT = 3002
};

enum ERxCleanupMsg {
    RX_MSG_POST_DONE = 4001,
    RX_MSG_POST_BATCH_COMPRESS = 4002
};

enum ERxReloadMsg {
    RX_MSG_RELOAD_CONFIG = 5001,
    RX_MSG_PDEF_ENDIAN_DETECTED = 5002  /* PDEF endian auto-detection result */
};

/* PDEF endian detection result message */
struct SRxPdefEndianMsg : public normal_msg {
    char pdef_file_path[256];  /* PDEF file path */
    int detected_endian;       /* ENDIAN_TYPE_BIG or ENDIAN_TYPE_LITTLE */

    SRxPdefEndianMsg()
        : normal_msg(RX_MSG_PDEF_ENDIAN_DETECTED), detected_endian(0)
    {
        memset(pdef_file_path, 0, sizeof(pdef_file_path));
    }
};

enum ERxFilterMsg {
    RX_MSG_PACKET_CAPTURED = 6001,  /* Packet captured, needs filtering */
    RX_MSG_PACKET_FILTERED = 6002   /* Packet filtered, ready to write */
};

enum ERxBizMsg {

    RX_MSG_BIZ_START = 10001,
    RX_MSG_BIZ_STATUS = 10002,
    RX_MSG_BIZ_STOP = 10003,

    RX_MSG_CAP_STARTED = 10100,
    RX_MSG_CAP_FINISHED = 10101
};

inline const char* rx_msg_type_to_string(int msg_type)
{

    if (msg_type == RX_MSG_NONE) return "RX_MSG_NONE";
    if (msg_type == 1) return "NORMAL_MSG_CONNECT";
    if (msg_type == 2) return "NORMAL_MSG_HTTP_REPLY";

    if (msg_type == RX_MSG_HTTP_REPLY) return "RX_MSG_HTTP_REPLY";

    if (msg_type == RX_MSG_START_CAPTURE) return "RX_MSG_START_CAPTURE";
    if (msg_type == RX_MSG_STOP_CAPTURE) return "RX_MSG_STOP_CAPTURE";
    if (msg_type == RX_MSG_QUERY_CAPTURE) return "RX_MSG_QUERY_CAPTURE";
    if (msg_type == RX_MSG_CLEAN_EXPIRED) return "RX_MSG_CLEAN_EXPIRED";
    if (msg_type == RX_MSG_COMPRESS_FILES) return "RX_MSG_COMPRESS_FILES";
    if (msg_type == RX_MSG_CHECK_THRESHOLD) return "RX_MSG_CHECK_THRESHOLD";
    if (msg_type == RX_MSG_TASK_UPDATE) return "RX_MSG_TASK_UPDATE";
    if (msg_type == RX_MSG_CAPTURE_START) return "RX_MSG_CAPTURE_START";
    if (msg_type == RX_MSG_CAPTURE_STOP) return "RX_MSG_CAPTURE_STOP";
    if (msg_type == RX_MSG_CAPTURE_CANCEL) return "RX_MSG_CAPTURE_CANCEL";
    if (msg_type == RX_MSG_CAPTURE_CONFIG_REFRESH) return "RX_MSG_CAPTURE_CONFIG_REFRESH";
    if (msg_type == RX_MSG_CAPTURE_STARTED) return "RX_MSG_CAPTURE_STARTED";
    if (msg_type == RX_MSG_CAPTURE_PROGRESS) return "RX_MSG_CAPTURE_PROGRESS";
    if (msg_type == RX_MSG_CAPTURE_FILE_READY) return "RX_MSG_CAPTURE_FILE_READY";
    if (msg_type == RX_MSG_CAPTURE_FINISHED) return "RX_MSG_CAPTURE_FINISHED";
    if (msg_type == RX_MSG_CAPTURE_FAILED) return "RX_MSG_CAPTURE_FAILED";
    if (msg_type == RX_MSG_CAPTURE_HEARTBEAT) return "RX_MSG_CAPTURE_HEARTBEAT";
    if (msg_type == RX_MSG_FILE_ENQUEUE) return "RX_MSG_FILE_ENQUEUE";
    if (msg_type == RX_MSG_CLEAN_CFG_REFRESH) return "RX_MSG_CLEAN_CFG_REFRESH";
    if (msg_type == RX_MSG_CLEAN_SHUTDOWN) return "RX_MSG_CLEAN_SHUTDOWN";
    if (msg_type == RX_MSG_CLEAN_STORED) return "RX_MSG_CLEAN_STORED";
    if (msg_type == RX_MSG_CLEAN_COMPRESS_DONE) return "RX_MSG_CLEAN_COMPRESS_DONE";
    if (msg_type == RX_MSG_CLEAN_COMPRESS_FAILED) return "RX_MSG_CLEAN_COMPRESS_FAILED";
    if (msg_type == RX_MSG_CLEAN_HEARTBEAT) return "RX_MSG_CLEAN_HEARTBEAT";

    if (msg_type == RX_MSG_SAMPLE_TRIGGER) return "RX_MSG_SAMPLE_TRIGGER";
    if (msg_type == RX_MSG_SAMPLE_ALERT) return "RX_MSG_SAMPLE_ALERT";

    if (msg_type == RX_MSG_POST_DONE) return "RX_MSG_POST_DONE";
    if (msg_type == RX_MSG_POST_BATCH_COMPRESS) return "RX_MSG_POST_BATCH_COMPRESS";

    if (msg_type == RX_MSG_RELOAD_CONFIG) return "RX_MSG_RELOAD_CONFIG";
    if (msg_type == RX_MSG_PDEF_ENDIAN_DETECTED) return "RX_MSG_PDEF_ENDIAN_DETECTED";

    if (msg_type == RX_MSG_BIZ_START) return "RX_MSG_BIZ_START";
    if (msg_type == RX_MSG_BIZ_STATUS) return "RX_MSG_BIZ_STATUS";
    if (msg_type == RX_MSG_BIZ_STOP) return "RX_MSG_BIZ_STOP";
    if (msg_type == RX_MSG_CAP_STARTED) return "RX_MSG_CAP_STARTED";
    if (msg_type == RX_MSG_CAP_FINISHED) return "RX_MSG_CAP_FINISHED";

    return "UNKNOWN_MSG";
}

namespace RxCaptureConstants {

    const int kCaptureIdStartValue = 1000;

    const int kMinConcurrentCaptures = 1;

}

#endif

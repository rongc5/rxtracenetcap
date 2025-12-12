#include "rxcapturethread.h"
#include "rxcapturemessages.h"
#include "legacy_core.h"

#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <cstring>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;
#include <vector>

CRxCaptureThread::CRxCaptureThread()
{
}

CRxCaptureThread::~CRxCaptureThread()
{
}

void CRxCaptureThread::handle_msg(shared_ptr<normal_msg>& msg)
{
    if (!msg) {
        return;
    }

    switch (msg->_msg_op) {
        case RX_MSG_CAPTURE_START:
        {
            shared_ptr<SRxCaptureStartMsgV2> start_v2 =
                dynamic_pointer_cast<SRxCaptureStartMsgV2>(msg);
            if (start_v2) {
                handle_capture_start_v2(start_v2.get());
            }
            break;
        }
        default:
        {
            LOG_DEBUG("Capture worker %u received unsupported message op=%d",
                      get_thread_index(), msg->_msg_op);
            break;
        }
    }
}

void CRxCaptureThread::handle_capture_start_v2(SRxCaptureStartMsgV2* msg)
{
    if (!msg) {
        return;
    }

    LOG_NOTICE("Capture worker %u starting capture task %d (key=%s, sid=%s)",
               get_thread_index(), msg->capture_id, msg->key.c_str(), msg->sid.c_str());


    execute_capture(*msg);
}


static CRxCaptureTaskCfg build_task_cfg(const CaptureSpec& spec,
                                        const CaptureConfigSnapshot& config)
{
    CRxCaptureTaskCfg cfg;
    cfg.iface = spec.resolved_iface.empty() ? spec.iface : spec.resolved_iface;
    cfg.bpf = spec.filter;
    cfg.outfile.clear();
    int duration = spec.max_duration_sec > 0 ? spec.max_duration_sec : config.max_duration_sec;
    cfg.duration_sec = duration < 0 ? 0 : duration;
    cfg.category = spec.category;
    cfg.file_pattern = !spec.output_pattern.empty() ? spec.output_pattern : config.filename_template;
    if (cfg.file_pattern.empty()) {
        cfg.file_pattern = "{day}/{date}-{iface}-{proc}-{port}.pcap";
    }
    long max_bytes = spec.max_bytes > 0 ? spec.max_bytes : config.max_bytes;
    cfg.max_bytes = max_bytes < 0 ? 0 : max_bytes;
    cfg.proc_name = spec.proc_name;
    cfg.protocol_filter = spec.protocol_filter;
    cfg.protocol_filter_inline = spec.protocol_filter_inline;
    cfg.port = spec.port_filter;

    fprintf(stderr, "[DEBUG] build_task_cfg: spec.protocol_filter='%s', spec.protocol_filter_inline='%s'\n",
            spec.protocol_filter.c_str(), spec.protocol_filter_inline.c_str());

    return cfg;
}

void CRxCaptureThread::execute_capture(const SRxCaptureStartMsgV2& start_msg)
{
    const CaptureSpec& spec = start_msg.spec;
    const CaptureConfigSnapshot& config = start_msg.config;
    int manager_thread_index = start_msg.sender_thread_index;



    CRxCaptureTaskCfg cfg = build_task_cfg(spec, config);


    CRxCaptureTaskInfo task_info;
    task_info.id = start_msg.capture_id;
    task_info.cfg = cfg;
    task_info.base_dir = config.output_dir.empty() ? std::string("capture_output") : config.output_dir;
    task_info.compress_enabled = config.compress_enabled;
    task_info.compress_remove_src = config.compress_remove_src;
    task_info.post_sink = NULL;
    task_info.running = true;
    task_info.exit_code = -1;
    task_info.packets = 0;
    task_info.stopping = false;
    task_info.resolved_path.clear();


    CRxCaptureJob job(cfg, &task_info);
    int64_t start_ts = rx_capture_now_usec();


    if (!job.prepare()) {
        send_failure(manager_thread_index, start_msg, ERR_START_TCPDUMP_FAILED,
                     "pcap_prepare_failed");
        return;
    }


    std::string initial_file = job.get_current_file();
    send_started(manager_thread_index, start_msg, start_ts,
                 static_cast<pid_t>(getpid()), initial_file);


    while (!job.is_done()) {
        int ret = job.run_once();
        if (ret < 0) {
            usleep(1000);
        }
    }


    job.cleanup();


    int64_t finish_ts = rx_capture_now_usec();
    unsigned long total_packets = job.get_packet_count();
    unsigned long total_bytes = job.get_bytes_written();

    CaptureResultStats result;
    result.total_packets = total_packets;
    result.total_bytes = total_bytes;
    result.start_ts = start_ts;
    result.finish_ts = finish_ts;
    result.exit_code = 0;


    std::vector<CaptureFileInfo> files;
    std::string final_path = job.get_final_path();
    if (!final_path.empty()) {
        CaptureFileInfo file_info;
        file_info.file_path = final_path;
        struct stat st;
        if (stat(final_path.c_str(), &st) == 0) {
            file_info.file_size = static_cast<unsigned long>(st.st_size);
        } else {
            file_info.file_size = total_bytes;
        }
        file_info.segment_index = 0;
        file_info.total_segments = 1;
        file_info.file_ready_ts = finish_ts;
        files.push_back(file_info);
    }


    fprintf(stderr, "[DEBUG CAPTURE] Checking PDEF filter: protocol_filter='%s', protocol_filter_inline='%s'\n",
            spec.protocol_filter.c_str(), spec.protocol_filter_inline.c_str());

    if (!spec.protocol_filter.empty() || !spec.protocol_filter_inline.empty()) {
        fprintf(stderr, "[DEBUG CAPTURE] PDEF filter needed, calling send_raw_file_for_filter() with final_path='%s'\n",
                final_path.c_str());


        send_raw_file_for_filter(manager_thread_index, start_msg, final_path,
                                  spec.protocol_filter, spec.protocol_filter_inline);

        fprintf(stderr, "[DEBUG CAPTURE] send_raw_file_for_filter() completed\n");

        LOG_NOTICE("Capture worker %u completed task %d (packets=%lu, bytes=%lu), sent to FilterThread for PDEF filtering",
                   get_thread_index(), start_msg.capture_id, total_packets, total_bytes);
    } else {

        if (!files.empty()) {
            send_file_ready(manager_thread_index, start_msg, files);
        }
        send_finished(manager_thread_index, start_msg, result);

        LOG_NOTICE("Capture worker %u completed task %d (packets=%lu, bytes=%lu, duration=%.2fs, no PDEF filter)",
                   get_thread_index(), start_msg.capture_id,
                   total_packets, total_bytes,
                   (finish_ts - start_ts) / 1000000.0);
    }
}


void CRxCaptureThread::send_started(int manager_thread_index,
                                    const SRxCaptureStartMsgV2& start_msg,
                                    int64_t start_ts_usec,
                                    pid_t capture_pid,
                                    const std::string& output_file)
{
    if (manager_thread_index <= 0) {
        return;
    }
    shared_ptr<SRxCaptureStartedMsgV2> started(new SRxCaptureStartedMsgV2());
    started->capture_id = start_msg.capture_id;
    started->key = start_msg.key;
    started->sid = start_msg.sid;
    started->op_version = start_msg.op_version;
    started->config_hash = start_msg.config_hash;
    started->sender_thread_index = static_cast<int>(get_thread_index());
    started->start_ts = start_ts_usec;
    started->capture_pid = capture_pid;
    started->output_file = output_file;

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(started);
    base_net_thread::put_obj_msg(target, base);
}

void CRxCaptureThread::send_file_ready(int manager_thread_index,
                                       const SRxCaptureStartMsgV2& start_msg,
                                       const std::vector<CaptureFileInfo>& files)
{
    if (manager_thread_index <= 0 || files.empty()) {
        return;
    }
    shared_ptr<SRxCaptureFileReadyMsgV2> ready(new SRxCaptureFileReadyMsgV2());
    ready->capture_id = start_msg.capture_id;
    ready->key = start_msg.key;
    ready->sid = start_msg.sid;
    ready->op_version = start_msg.op_version;
    ready->config_hash = start_msg.config_hash;
    ready->sender_thread_index = static_cast<int>(get_thread_index());
    ready->files = files;

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(ready);
    base_net_thread::put_obj_msg(target, base);
}

void CRxCaptureThread::send_finished(int manager_thread_index,
                                     const SRxCaptureStartMsgV2& start_msg,
                                     const CaptureResultStats& stats)
{
    if (manager_thread_index <= 0) {
        return;
    }
    shared_ptr<SRxCaptureFinishedMsgV2> finished(new SRxCaptureFinishedMsgV2());
    finished->capture_id = start_msg.capture_id;
    finished->key = start_msg.key;
    finished->sid = start_msg.sid;
    finished->op_version = start_msg.op_version;
    finished->config_hash = start_msg.config_hash;
    finished->sender_thread_index = static_cast<int>(get_thread_index());
    finished->result = stats;

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(finished);
    base_net_thread::put_obj_msg(target, base);
}

void CRxCaptureThread::send_failure(int manager_thread_index,
                                    const SRxCaptureStartMsgV2& start_msg,
                                    ECaptureErrorCode error_code,
                                    const std::string& error_message)
{
    if (manager_thread_index <= 0) {
        return;
    }
    shared_ptr<SRxCaptureFailedMsgV2> failed(new SRxCaptureFailedMsgV2());
    failed->capture_id = start_msg.capture_id;
    failed->key = start_msg.key;
    failed->sid = start_msg.sid;
    failed->op_version = start_msg.op_version;
    failed->config_hash = start_msg.config_hash;
    failed->sender_thread_index = static_cast<int>(get_thread_index());
    failed->error_code = error_code;
    failed->error_message = error_message;

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(failed);
    base_net_thread::put_obj_msg(target, base);
}

void CRxCaptureThread::send_raw_file_for_filter(int manager_thread_index,
                                                 const SRxCaptureStartMsgV2& start_msg,
                                                 const std::string& raw_pcap_path,
                                                 const std::string& pdef_file_path,
                                                 const std::string& pdef_inline_content)
{
    fprintf(stderr, "[DEBUG SEND_RAW] Called with manager_thread_index=%d, raw_pcap_path='%s', pdef_file_path='%s'\n",
            manager_thread_index, raw_pcap_path.c_str(), pdef_file_path.c_str());

    if (manager_thread_index <= 0) {
        fprintf(stderr, "[DEBUG SEND_RAW] ERROR: manager_thread_index <= 0, returning\n");
        return;
    }

    shared_ptr<SRxCaptureRawFileMsgV2> raw_file(new SRxCaptureRawFileMsgV2());
    raw_file->capture_id = start_msg.capture_id;
    raw_file->key = start_msg.key;
    raw_file->sid = start_msg.sid;
    raw_file->op_version = start_msg.op_version;
    raw_file->config_hash = start_msg.config_hash;
    raw_file->sender_thread_index = static_cast<int>(get_thread_index());
    raw_file->raw_pcap_path = raw_pcap_path;
    raw_file->pdef_file_path = pdef_file_path;
    raw_file->pdef_inline_content = pdef_inline_content;
    raw_file->has_pdef_filter = !pdef_file_path.empty() || !pdef_inline_content.empty();

    fprintf(stderr, "[DEBUG SEND_RAW] Sending RX_MSG_CAPTURE_RAW_FILE message to manager thread %d\n", manager_thread_index);

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(raw_file);
    base_net_thread::put_obj_msg(target, base);

    fprintf(stderr, "[DEBUG SEND_RAW] Message sent successfully\n");
}

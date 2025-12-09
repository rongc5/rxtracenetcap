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
#include <map>

struct CRxCaptureThread::ActiveCaptureSession {
    CRxCaptureThread* owner;
    SRxCaptureStartMsgV2 start_msg;
    CaptureConfigSnapshot config;
    CaptureSpec spec;
    CRxCaptureTaskInfo* task_info;
    int manager_thread_index;
    pthread_t thread;
    pthread_mutex_t lock;
    bool stop_requested;
    bool finished;
    ECaptureErrorCode stop_reason;
    std::string stop_message;

    ActiveCaptureSession()
        : owner(NULL)
        , task_info(NULL)
        , manager_thread_index(0)
        , thread(0)
        , stop_requested(false)
        , finished(false)
        , stop_reason(ERR_NONE)
    {
        pthread_mutex_init(&lock, NULL);
    }

    ~ActiveCaptureSession()
    {
        pthread_mutex_destroy(&lock);
        if (task_info) {
            delete task_info;
            task_info = NULL;
        }
    }

    void mark_stop_requested(ECaptureErrorCode code, const std::string& message)
    {
        pthread_mutex_lock(&lock);
        stop_requested = true;
        stop_reason = code;
        stop_message = message;
        if (task_info) {
            task_info->stopping = true;
        }
        pthread_mutex_unlock(&lock);
    }
};

CRxCaptureThread::CRxCaptureThread()
{
    pthread_mutex_init(&sessions_mutex_, NULL);
}

CRxCaptureThread::~CRxCaptureThread()
{
    pthread_mutex_lock(&sessions_mutex_);
    std::map<int, ActiveCaptureSession*>::iterator it = active_sessions_.begin();
    while (it != active_sessions_.end()) {
        ActiveCaptureSession* session = it->second;
        pthread_t th = session ? session->thread : 0;
        if (session) {
            session->mark_stop_requested(ERR_RUN_CANCELLED, "worker_shutdown");
        }
        ++it;
        if (session) {
            pthread_mutex_unlock(&sessions_mutex_);
            if (th) {
                pthread_join(th, NULL);
            }
            delete session;
            pthread_mutex_lock(&sessions_mutex_);
        }
    }
    active_sessions_.clear();
    pthread_mutex_unlock(&sessions_mutex_);
    pthread_mutex_destroy(&sessions_mutex_);
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
        case RX_MSG_CAPTURE_STOP:
        {
            shared_ptr<SRxCaptureStopMsgV2> stop_v2 =
                dynamic_pointer_cast<SRxCaptureStopMsgV2>(msg);
            if (stop_v2) {
                handle_capture_stop_v2(stop_v2.get());
            }
            break;
        }
        case RX_MSG_CAPTURE_CANCEL:
        {
            shared_ptr<SRxCaptureCancelMsgV2> cancel_v2 =
                dynamic_pointer_cast<SRxCaptureCancelMsgV2>(msg);
            if (cancel_v2) {
                handle_capture_cancel_v2(cancel_v2.get());
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

    ActiveCaptureSession* session = new ActiveCaptureSession();
    if (!session) {
        return;
    }

    session->owner = this;
    session->start_msg = *msg;
    session->config = msg->config;
    session->spec = msg->spec;
    session->manager_thread_index = msg->sender_thread_index;
    session->task_info = new CRxCaptureTaskInfo();
    if (!session->task_info) {
        delete session;
        return;
    }

    register_session(session);

    int rc = pthread_create(&session->thread, NULL, &CRxCaptureThread::capture_thread_entry, session);
    if (rc != 0) {
        unregister_session(msg->capture_id);
        shared_ptr<SRxCaptureFailedMsgV2> failed(new SRxCaptureFailedMsgV2());
        failed->capture_id = msg->capture_id;
        failed->key = msg->key;
        failed->sid = msg->sid;
        failed->op_version = msg->op_version;
        failed->config_hash = msg->config_hash;
        failed->sender_thread_index = static_cast<int>(get_thread_index());
        failed->error_code = ERR_UNKNOWN;
        failed->error_message = "spawn_capture_thread_failed";
        if (session->manager_thread_index > 0) {
            ObjId target;
            target._id = OBJ_ID_THREAD;
            target._thread_index = static_cast<uint32_t>(session->manager_thread_index);
            shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(failed);
            base_net_thread::put_obj_msg(target, base);
        }
        delete session;
    }
}

void CRxCaptureThread::handle_capture_stop_v2(SRxCaptureStopMsgV2* msg)
{
    if (!msg) {
        return;
    }
    request_stop(msg->capture_id, ERR_RUN_CANCELLED, msg->stop_reason);
}

void CRxCaptureThread::handle_capture_cancel_v2(SRxCaptureCancelMsgV2* msg)
{
    if (!msg) {
        return;
    }
    request_stop(msg->capture_id, msg->error_code, msg->cancel_reason);
}

void* CRxCaptureThread::capture_thread_entry(void* arg)
{
    ActiveCaptureSession* session = static_cast<ActiveCaptureSession*>(arg);
    if (!session || !session->owner) {
        return NULL;
    }

    session->owner->run_capture_session(session);
    session->owner->on_session_finished(session);
    return NULL;
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
    return cfg;
}

void CRxCaptureThread::run_capture_session(ActiveCaptureSession* session)
{
    if (!session || !session->task_info) {
        return;
    }

    const CaptureSpec& spec = session->spec;
    const CaptureConfigSnapshot& config = session->config;
    int manager_thread_index = session->manager_thread_index;

    CRxCaptureTaskCfg cfg = build_task_cfg(spec, config);

    CRxCaptureTaskInfo fresh_info;
    *session->task_info = fresh_info;
    session->task_info->id = session->start_msg.capture_id;
    session->task_info->cfg = cfg;
    session->task_info->base_dir = config.output_dir.empty() ? std::string("capture_output") : config.output_dir;
    session->task_info->compress_enabled = config.compress_enabled;
    session->task_info->compress_cmd = config.compress_format;
    session->task_info->compress_remove_src = config.compress_remove_src;
    session->task_info->post_sink = NULL;
    session->task_info->running = true;
    session->task_info->exit_code = -1;
    session->task_info->packets = 0;
    session->task_info->stopping = session->stop_requested;
    session->task_info->resolved_path.clear();

    CRxCaptureJob job(cfg, session->task_info);
    int64_t start_ts = rx_capture_now_usec();

    if (!job.prepare()) {
        CaptureProgressStats failure_progress;
        failure_progress.first_packet_ts = start_ts;
        failure_progress.last_packet_ts = start_ts;
        send_failure(manager_thread_index, session->start_msg, ERR_START_TCPDUMP_FAILED,
                     "pcap_prepare_failed", failure_progress);
        session->finished = true;
        return;
    }

    std::string initial_file = job.get_current_file();
    send_started(manager_thread_index, session->start_msg, start_ts,
                 static_cast<pid_t>(getpid()), initial_file);

    unsigned long last_report_packets = 0;
    unsigned long last_report_bytes = 0;
    int64_t last_report_ts = start_ts;
    int64_t first_packet_ts = 0;
    CaptureProgressStats progress_stats;
    progress_stats.first_packet_ts = 0;
    progress_stats.last_packet_ts = start_ts;

    const int64_t interval_us = (config.progress_interval_sec > 0)
        ? static_cast<int64_t>(config.progress_interval_sec) * 1000000LL
        : 0;
    const unsigned long packet_threshold = config.progress_packet_threshold > 0
        ? static_cast<unsigned long>(config.progress_packet_threshold)
        : 0;
    const unsigned long bytes_threshold = config.progress_bytes_threshold > 0
        ? static_cast<unsigned long>(config.progress_bytes_threshold)
        : 0;

    while (!job.is_done()) {
        int ret = job.run_once();
        int64_t now = rx_capture_now_usec();
        if (ret < 0) {
            usleep(1000);
        }

        unsigned long packets = job.get_packet_count();
        unsigned long bytes = job.get_bytes_written();
        if (ret > 0) {
            if (first_packet_ts == 0) {
                first_packet_ts = now;
            }
            progress_stats.last_packet_ts = now;
        }

        bool should_report = false;
        if (interval_us > 0 && (now - last_report_ts) >= interval_us) {
            should_report = true;
        } else if (packet_threshold > 0 && (packets - last_report_packets) >= packet_threshold) {
            should_report = true;
        } else if (bytes_threshold > 0 && (bytes - last_report_bytes) >= bytes_threshold) {
            should_report = true;
        }

        if (should_report && manager_thread_index > 0) {
            progress_stats.packets = packets;
            progress_stats.bytes = bytes;
            progress_stats.first_packet_ts = first_packet_ts == 0 ? start_ts : first_packet_ts;
            progress_stats.file_size = bytes;
            progress_stats.cpu_seconds = 0.0;
            if (progress_stats.last_packet_ts == 0) {
                progress_stats.last_packet_ts = now;
            }
            send_progress(manager_thread_index, session->start_msg, progress_stats);
            last_report_packets = packets;
            last_report_bytes = bytes;
            last_report_ts = now;
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

    bool stopped = false;
    pthread_mutex_lock(&session->lock);
    if (session->stop_requested || session->task_info->stopping) {
        stopped = true;
        if (session->stop_reason == ERR_NONE) {
            session->stop_reason = ERR_RUN_CANCELLED;
        }
        result.exit_code = session->stop_reason;
        result.error_message = session->stop_message.empty() ? "capture_stopped" : session->stop_message;
    }
    pthread_mutex_unlock(&session->lock);

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

    if (!files.empty()) {
        send_file_ready(manager_thread_index, session->start_msg, files);
    }

    if (stopped && result.exit_code == ERR_RUN_CANCELLED) {

        send_finished(manager_thread_index, session->start_msg, result);
    } else if (result.exit_code == 0) {
        send_finished(manager_thread_index, session->start_msg, result);
    } else {
        CaptureProgressStats last_progress;
        last_progress.packets = total_packets;
        last_progress.bytes = total_bytes;
        last_progress.first_packet_ts = start_ts;
        last_progress.last_packet_ts = finish_ts;
        send_failure(manager_thread_index, session->start_msg,
                     static_cast<ECaptureErrorCode>(result.exit_code),
                     result.error_message, last_progress);
    }

    session->finished = true;
}

void CRxCaptureThread::register_session(ActiveCaptureSession* session)
{
    if (!session) {
        return;
    }
    pthread_mutex_lock(&sessions_mutex_);
    active_sessions_[session->start_msg.capture_id] = session;
    pthread_mutex_unlock(&sessions_mutex_);
}

void CRxCaptureThread::unregister_session(int capture_id)
{
    pthread_mutex_lock(&sessions_mutex_);
    active_sessions_.erase(capture_id);
    pthread_mutex_unlock(&sessions_mutex_);
}

void CRxCaptureThread::request_stop(int capture_id,
                                    ECaptureErrorCode reason_code,
                                    const std::string& reason)
{
    pthread_mutex_lock(&sessions_mutex_);
    std::map<int, ActiveCaptureSession*>::iterator it = active_sessions_.find(capture_id);
    if (it == active_sessions_.end()) {
        pthread_mutex_unlock(&sessions_mutex_);
        LOG_WARNING("request_stop: capture %d not found", capture_id);
        return;
    }
    ActiveCaptureSession* session = it->second;
    if (session) {
        session->mark_stop_requested(reason_code, reason);
    }
    pthread_mutex_unlock(&sessions_mutex_);
}

void CRxCaptureThread::on_session_finished(ActiveCaptureSession* session)
{
    if (!session) {
        return;
    }
    unregister_session(session->start_msg.capture_id);
    delete session;
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

void CRxCaptureThread::send_progress(int manager_thread_index,
                                     const SRxCaptureStartMsgV2& start_msg,
                                     const CaptureProgressStats& stats)
{
    if (manager_thread_index <= 0) {
        return;
    }
    shared_ptr<SRxCaptureProgressMsgV2> progress(new SRxCaptureProgressMsgV2());
    progress->capture_id = start_msg.capture_id;
    progress->key = start_msg.key;
    progress->sid = start_msg.sid;
    progress->op_version = start_msg.op_version;
    progress->config_hash = start_msg.config_hash;
    progress->sender_thread_index = static_cast<int>(get_thread_index());
    progress->progress = stats;
    if (progress->progress.last_packet_ts == 0) {
        progress->progress.last_packet_ts = rx_capture_now_usec();
    }
    if (progress->progress.first_packet_ts == 0) {
        progress->progress.first_packet_ts = start_msg.ts_usec;
    }
    progress->report_ts = rx_capture_now_usec();

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(progress);
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
                                    const std::string& error_message,
                                    const CaptureProgressStats& last_progress)
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
    failed->last_progress = last_progress;
    if (failed->last_progress.first_packet_ts == 0) {
        failed->last_progress.first_packet_ts = start_msg.ts_usec;
    }
    if (failed->last_progress.last_packet_ts == 0) {
        failed->last_progress.last_packet_ts = rx_capture_now_usec();
    }

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(failed);
    base_net_thread::put_obj_msg(target, base);
}

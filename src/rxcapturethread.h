#ifndef RX_CAPTURE_THREAD_H
#define RX_CAPTURE_THREAD_H

#include "legacy_core.h"
#include "rxcapturemanager.h"
#include "rxcapturesession.h"
#include "rxcapturemessages.h"
#include <vector>
#include <map>
#include <pthread.h>

struct SRxCaptureRequest : public normal_msg {
    CRxCaptureTaskCfg cfg;
    CRxCaptureTaskInfo* parent_task_info;
    SRxCaptureRequest() : parent_task_info(NULL) { _msg_op = RX_MSG_START_CAPTURE; }
};

class CRxCaptureThread : public base_net_thread {
public:
    CRxCaptureThread();
    ~CRxCaptureThread();

protected:

    virtual void handle_msg(shared_ptr<normal_msg>& msg);

private:
    void handle_capture_start_v2(SRxCaptureStartMsgV2* msg);
    void handle_capture_stop_v2(SRxCaptureStopMsgV2* msg);
    void handle_capture_cancel_v2(SRxCaptureCancelMsgV2* msg);
    void send_started(int manager_thread_index,
                      const SRxCaptureStartMsgV2& start_msg,
                      int64_t start_ts_usec,
                      pid_t capture_pid,
                      const std::string& output_file);
    void send_progress(int manager_thread_index,
                       const SRxCaptureStartMsgV2& start_msg,
                       const CaptureProgressStats& stats);
    void send_file_ready(int manager_thread_index,
                         const SRxCaptureStartMsgV2& start_msg,
                         const std::vector<CaptureFileInfo>& files);
    void send_finished(int manager_thread_index,
                       const SRxCaptureStartMsgV2& start_msg,
                       const CaptureResultStats& stats);
    void send_failure(int manager_thread_index,
                      const SRxCaptureStartMsgV2& start_msg,
                      ECaptureErrorCode error_code,
                      const std::string& error_message,
                      const CaptureProgressStats& last_progress);

    struct ActiveCaptureSession;
    static void* capture_thread_entry(void* arg);
    void run_capture_session(ActiveCaptureSession* session);
    void register_session(ActiveCaptureSession* session);
    void unregister_session(int capture_id);
    void request_stop(int capture_id,
                      ECaptureErrorCode reason_code,
                      const std::string& reason);
    void on_session_finished(ActiveCaptureSession* session);

    CRxCaptureThread(const CRxCaptureThread&);
    CRxCaptureThread& operator=(const CRxCaptureThread&);

private:
    std::map<int, ActiveCaptureSession*> active_sessions_;
    pthread_mutex_t sessions_mutex_;
};

#endif

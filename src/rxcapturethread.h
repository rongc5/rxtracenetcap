#ifndef RX_CAPTURE_THREAD_H
#define RX_CAPTURE_THREAD_H

#include "legacy_core.h"
#include "rxcapturemanager.h"
#include "rxcapturesession.h"
#include "rxcapturemessages.h"





class CRxCaptureThread : public base_net_thread {
public:
    CRxCaptureThread();
    ~CRxCaptureThread();

protected:
    virtual void handle_msg(shared_ptr<normal_msg>& msg);

private:
    void handle_capture_start_v2(SRxCaptureStartMsgV2* msg);


    void execute_capture(const SRxCaptureStartMsgV2& start_msg);


    void send_started(int manager_thread_index,
                      const SRxCaptureStartMsgV2& start_msg,
                      int64_t start_ts_usec,
                      pid_t capture_pid,
                      const std::string& output_file);

    void send_file_ready(int manager_thread_index,
                         const SRxCaptureStartMsgV2& start_msg,
                         const std::vector<CaptureFileInfo>& files);

    void send_finished(int manager_thread_index,
                       const SRxCaptureStartMsgV2& start_msg,
                       const CaptureResultStats& stats);

    void send_failure(int manager_thread_index,
                      const SRxCaptureStartMsgV2& start_msg,
                      ECaptureErrorCode error_code,
                      const std::string& error_message);

    void send_raw_file_for_filter(int manager_thread_index,
                                   const SRxCaptureStartMsgV2& start_msg,
                                   const std::string& raw_pcap_path,
                                   const std::string& pdef_file_path,
                                   const std::string& pdef_inline_content);

    CRxCaptureThread(const CRxCaptureThread&);
    CRxCaptureThread& operator=(const CRxCaptureThread&);
};

#endif

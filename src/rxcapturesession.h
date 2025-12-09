#ifndef RX_CAPTURE_SESSION_H
#define RX_CAPTURE_SESSION_H

#include "rxcapturemanager.h"
#include "rxstorageutils.h"
#include "rxfilterthread.h"
#include <pcap/pcap.h>
#include <string>

class CRxCaptureJob {
public:
    CRxCaptureJob(const CRxCaptureTaskCfg& cfg, const CRxCaptureTaskInfo* parent_task_info);
    ~CRxCaptureJob();

    bool prepare();

    int run_once();

    void cleanup();

    bool is_done() const;

    unsigned long get_packet_count() const;

    std::string get_final_path() const;

    std::string get_current_file() const;

    unsigned long get_bytes_written() const;

    CRxFilterThread* get_filter_thread() { return filter_thread_; }
    uint32_t get_filter_thread_index() const;

private:

    CRxCaptureJob(const CRxCaptureJob&);
    CRxCaptureJob& operator=(const CRxCaptureJob&);

    const CRxCaptureTaskCfg cfg_;
    const CRxCaptureTaskInfo* parent_task_info_;

    pcap_t* pcap_handle_;
    CRxDumpCtx dumper_context_;

    bool done_;
    unsigned long packets_;
    unsigned long end_time_sec_;

    CRxFilterThread* filter_thread_;
    bool use_filter_thread_;
};

#endif

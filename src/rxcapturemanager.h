#ifndef RXNET_CAPTURE_MANAGER_H
#define RXNET_CAPTURE_MANAGER_H

#include <pthread.h>
#include <string>

struct CRxCaptureTaskCfg {
    std::string iface;
    std::string bpf;
    std::string outfile;
    int duration_sec;
    std::string category;
    std::string file_pattern;
    long max_bytes;
    std::string proc_name;
    std::string protocol_filter;
    std::string protocol_filter_inline;
    int port;
    CRxCaptureTaskCfg() : duration_sec(0), max_bytes(0), port(0) {}
};

struct CRxCaptureTaskInfo {
    int id;
    CRxCaptureTaskCfg cfg;
    pthread_t thread;
    volatile bool stopping;
    volatile bool running;
    volatile unsigned long packets;
    volatile int exit_code;
    volatile long finish_timestamp;
    std::string base_dir;
    bool compress_enabled;
    bool compress_remove_src;
    void* post_sink;
    std::string resolved_path;
    void* reserved_ctx;
    CRxCaptureTaskInfo()
        : id(0),
          stopping(false),
          running(false),
          packets(0),
          exit_code(0),
          finish_timestamp(0),
          compress_enabled(false),
          compress_remove_src(false),
          post_sink(NULL),
          reserved_ctx(NULL) {}
};

#endif

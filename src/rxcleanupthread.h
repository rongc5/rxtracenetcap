#ifndef RX_CLEANUP_THREAD_H
#define RX_CLEANUP_THREAD_H

#include "legacy_core.h"
#include "rxcapturemessages.h"
#include "rxserverconfig.h"
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;
#include <vector>

class CRxCleanupThread : public base_net_thread {
public:
    CRxCleanupThread();
    ~CRxCleanupThread();

    bool start();
    void configure(const CRxServerConfig::CleanupConfig& cfg);

protected:
    virtual void handle_msg(shared_ptr<normal_msg>& msg);
    virtual void handle_timeout(shared_ptr<timer_msg>& t_msg);

private:
    struct PendingFile {
        int capture_id;
        std::string key;
        std::string sid;
        CaptureFileInfo file;
        CaptureConfigSnapshot policy;
    };
    enum { CLEANUP_TIMER_TYPE = 1 };
    void schedule_cleanup_timer();
    void do_cleanup();
    void cleanup_pdef_temp_files();
    void enqueue_files(const SRxFileEnqueueMsgV2& enqueue_msg);
    void process_pending_files();
    bool compress_file(const PendingFile& pending, CaptureArchiveInfo& archive, std::string& error_msg);
    std::string record_file_metadata(int capture_id, const std::string& key, const CaptureFileInfo& info);
    void rotate_record_file_if_needed(size_t incoming_size);
    void prune_record_files();
    std::string current_record_file() const;
    void notify_archive_result(int capture_id, const std::string& key, const std::string& sid, const CaptureArchiveInfo& archive);
    void notify_archive_failure(int capture_id, const std::string& key, const std::string& sid, const std::vector<CaptureFileInfo>& files, const std::string& error);
    bool should_compress_size(unsigned long file_size, int policy_threshold_mb) const;
    void prune_archives();

    int cleanup_interval_sec_;
    CRxServerConfig::CleanupConfig config_;
    std::string pdef_dir_;
    long pdef_ttl_seconds_;
    std::string record_file_path_;
    unsigned long current_record_size_;
    unsigned long record_max_size_bytes_;
    unsigned long compress_threshold_bytes_;
    unsigned long long archive_max_total_bytes_;
    long archive_retention_seconds_;
    std::vector<PendingFile> pending_files_;
};

#endif

#ifndef RX_CAPTURE_MANAGER_THREAD_H
#define RX_CAPTURE_MANAGER_THREAD_H

#include "legacy_core.h"
#include "rxmsgtypes.h"
#include "rxcapturetasktypes.h"
#include "rxprocessresolver.h"
#include "rxcapturemessages.h"
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;
#include <map>
#include <string>
#include <vector>
#include <sys/types.h>

enum ERxCaptureTimer {
    TIMER_TYPE_QUEUE_CHECK = 1,
    TIMER_TYPE_EXPIRE_CLEAN = 2,
    TIMER_TYPE_BATCH_COMPRESS = 3
};

struct SRxSampleMsg;

class CRxCaptureManagerThread : public base_net_thread {
public:
    CRxCaptureManagerThread();
    virtual ~CRxCaptureManagerThread();

    bool start();

    virtual void run_process();
    virtual void handle_msg(shared_ptr<normal_msg>& msg);
    virtual void handle_timeout(shared_ptr<timer_msg>& t_msg);

    void add_worker_thread(uint32_t thread_index);

    const std::vector<uint32_t>& get_worker_threads() const { return _worker_thd_vec; }

    bool GetTaskById(int capture_id, SRxCaptureTask& task);
    bool UpdateTaskStatus(int capture_id, ECaptureTaskStatus new_status);

private:
    void handle_start_capture(shared_ptr<normal_msg>& msg);
    void handle_stop_capture(shared_ptr<normal_msg>& msg);
    void handle_query_capture(shared_ptr<normal_msg>& msg);
    void handle_task_update(shared_ptr<normal_msg>& msg);
    void handle_capture_started_v2(shared_ptr<normal_msg>& msg);
    void handle_capture_progress_v2(shared_ptr<normal_msg>& msg);
    void handle_capture_file_ready_v2(shared_ptr<normal_msg>& msg);
    void handle_capture_finished_v2(shared_ptr<normal_msg>& msg);
    void handle_capture_failed_v2(shared_ptr<normal_msg>& msg);
    void handle_clean_expired(shared_ptr<normal_msg>& msg);
    void handle_compress_files(shared_ptr<normal_msg>& msg);
    void handle_check_threshold(shared_ptr<normal_msg>& msg);
    void handle_clean_compress_done(shared_ptr<normal_msg>& msg);
    void handle_clean_compress_failed(shared_ptr<normal_msg>& msg);
    void handle_sample_alert(shared_ptr<normal_msg>& msg);
    bool prepare_capture_from_sample(const shared_ptr<SRxSampleMsg>& alert,
                                     shared_ptr<SRxStartCaptureMsg>& start_msg);
    bool should_throttle_module(const std::string& module_name,
                                int cooldown_sec,
                                time_t now);
    void clear_module_cooldown_for_capture(int capture_id);

    void start_queue_timer();
    void start_clean_timer();
    void start_compress_timer();

    void check_queue();
    void clean_expired_files();
    void batch_compress_files();
    void check_system_threshold();

    bool resolve_target_processes(shared_ptr<SRxStartCaptureMsg>& start_msg,
                                   std::vector<SProcessInfo>& matched_processes,
                                   shared_ptr<SRxHttpReplyMsg>& error_reply);
    std::string generate_task_key(const shared_ptr<SRxStartCaptureMsg>& start_msg);
    bool check_task_duplicate(const std::string& task_key, int& existing_id, std::string& existing_status);
    int get_max_concurrent_limit();
    void count_active_tasks(size_t& running_count, size_t& pending_count);
    void create_duplicate_error_reply(shared_ptr<SRxHttpReplyMsg>& reply,
                                       const std::string& task_key,
                                       const std::string& sid,
                                       int existing_id,
                                       const std::string& status);
    void create_capacity_error_reply(shared_ptr<SRxHttpReplyMsg>& reply,
                                      int max_concurrent, size_t running_count, size_t pending_count);
    int create_and_add_capture_task(const shared_ptr<SRxStartCaptureMsg>& start_msg,
                                     const std::string& task_key,
                                     const std::string& signature,
                                     const std::string& sid,
                                     const std::vector<SProcessInfo>& matched_processes,
                                     shared_ptr<SRxHttpReplyMsg>& reply);
    void dispatch_task_to_worker(int capture_id,
                                 const std::string& task_key,
                                 const std::string& sid,
                                 const CaptureSpec& spec,
                                 const CaptureConfigSnapshot& config_snapshot,
                                 shared_ptr<SRxStartCaptureMsg>& legacy_msg);
    void send_reply_to_http(const ObjId& reply_target, shared_ptr<SRxHttpReplyMsg>& reply);

private:
    bool _is_first;

    std::vector<class CRxCaptureThread*> _capture_threads;

    std::vector<uint32_t> _worker_thd_vec;
    std::map<std::string, time_t> _module_last_trigger;
};

#endif

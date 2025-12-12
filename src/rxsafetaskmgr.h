#ifndef __SAFE_TASK_MGR_H__
#define __SAFE_TASK_MGR_H__

#include "rxcapturetasktypes.h"
#include <map>
#include <vector>
#include <string>
#include <stdint.h>
#include <time.h>

struct TaskSnapshot
{
    int capture_id;
    std::string key;
    std::string signature;
    std::string sid;
    ECaptureTaskStatus status;
    ECaptureMode capture_mode;
    std::string iface;
    std::string proc_name;
    pid_t target_pid;
    uint32_t worker_thread_index;
    bool stop_requested;
    bool cancel_requested;
    std::string filter;
    int port_filter;
    int duration_sec;
    long start_time;
    long end_time;
    unsigned long packet_count;
    unsigned long bytes_captured;
    std::string error_message;
    std::string client_ip;
    std::string request_user;
    std::vector<CaptureFileInfo> captured_files;
    std::vector<CaptureArchiveInfo> archives;

    TaskSnapshot()
        : capture_id(-1)
        , status(STATUS_PENDING)
        , capture_mode(MODE_INTERFACE)
        , target_pid(-1)
        , worker_thread_index(0)
        , stop_requested(false)
        , cancel_requested(false)
        , port_filter(0)
        , duration_sec(0)
        , start_time(0)
        , end_time(0)
        , packet_count(0)
        , bytes_captured(0)
    {
    }
};

struct TaskStats
{
    size_t total_count;
    size_t pending_count;
    size_t resolving_count;
    size_t running_count;
    size_t completed_count;
    size_t failed_count;
    size_t stopped_count;

    TaskStats()
        : total_count(0)
        , pending_count(0)
        , resolving_count(0)
        , running_count(0)
        , completed_count(0)
        , failed_count(0)
        , stopped_count(0)
    {
    }
};

class CRxTaskSlot
{
public:
    CRxTaskSlot() : _task_ptr(NULL) {}

    CRxTaskSlot(const CRxTaskSlot& other) : _task_ptr(NULL)
    {
        SRxCaptureTask* task = __atomic_load_n(&other._task_ptr, __ATOMIC_ACQUIRE);
        __atomic_store_n(&_task_ptr, task, __ATOMIC_RELEASE);
    }

    CRxTaskSlot& operator=(const CRxTaskSlot& other)
    {
        if (this != &other) {
            SRxCaptureTask* task = __atomic_load_n(&other._task_ptr, __ATOMIC_ACQUIRE);
            __atomic_store_n(&_task_ptr, task, __ATOMIC_RELEASE);
        }
        return *this;
    }

    ~CRxTaskSlot()
    {

    }

    SRxCaptureTask* get() const
    {
        return __atomic_load_n(&_task_ptr, __ATOMIC_ACQUIRE);
    }

    SRxCaptureTask* exchange(SRxCaptureTask* new_task)
    {
        return __atomic_exchange_n(&_task_ptr, new_task, __ATOMIC_ACQ_REL);
    }

    void set(SRxCaptureTask* new_task)
    {
        __atomic_store_n(&_task_ptr, new_task, __ATOMIC_RELEASE);
    }

private:
    static long usec_to_sec(int64_t ts_usec)
    {
        if (ts_usec <= 0) {
            return 0;
        }
        return static_cast<long>(ts_usec / 1000000LL);
    }

    mutable SRxCaptureTask* _task_ptr;
};

struct TaskTable
{
    std::map<int, CRxTaskSlot> slots;
    std::map<std::string, int> key_to_id;
    std::map<std::string, int> signature_to_id;
    std::map<std::string, int> sid_to_id;

    void add_task(int capture_id,
                  const std::string& key,
                  const std::string& signature,
                  const std::string& sid,
                  SRxCaptureTask* task)
    {
        task->signature = signature;
        task->sid = sid;
        slots[capture_id].set(task);
        key_to_id[key] = capture_id;
        if (!signature.empty() && is_active_status(task->status)) {
            signature_to_id[signature] = capture_id;
        }
        if (!sid.empty()) {
            sid_to_id[sid] = capture_id;
        }
    }

    SRxCaptureTask* remove_task(int capture_id)
    {
        std::map<int, CRxTaskSlot>::iterator slot_it = slots.find(capture_id);
        if (slot_it == slots.end()) {
            return NULL;
        }

        SRxCaptureTask* old_task = slot_it->second.exchange(NULL);

        if (old_task) {
            key_to_id.erase(old_task->key);
            if (!old_task->signature.empty()) {
                std::map<std::string, int>::iterator sig_it = signature_to_id.find(old_task->signature);
                if (sig_it != signature_to_id.end() && sig_it->second == capture_id) {
                    signature_to_id.erase(sig_it);
                }
            }
            if (!old_task->sid.empty()) {
                std::map<std::string, int>::iterator sig_it = sid_to_id.find(old_task->sid);
                if (sig_it != sid_to_id.end() && sig_it->second == capture_id) {
                    sid_to_id.erase(sig_it);
                }
            }
        }

        slots.erase(slot_it);

        return old_task;
    }

    bool query_task(int capture_id, TaskSnapshot& snapshot) const
    {
        std::map<int, CRxTaskSlot>::const_iterator it = slots.find(capture_id);
        if (it == slots.end()) {
            return false;
        }

        SRxCaptureTask* task = it->second.get();
        if (!task) {
            return false;
        }

        snapshot.capture_id = task->capture_id;
        snapshot.key = task->key;
        snapshot.signature = task->signature;
        snapshot.sid = task->sid;
        snapshot.status = task->status;
        snapshot.capture_mode = task->capture_mode;
        snapshot.iface = task->iface;
        snapshot.proc_name = task->proc_name;
        snapshot.target_pid = task->target_pid;
        snapshot.worker_thread_index = task->worker_thread_index;
        snapshot.stop_requested = task->stop_requested;
        snapshot.cancel_requested = task->cancel_requested;
        snapshot.filter = task->filter;
        snapshot.port_filter = task->port_filter;
        snapshot.duration_sec = task->duration_sec;
        snapshot.start_time = task->start_time;
        snapshot.end_time = task->end_time;
        snapshot.packet_count = task->packet_count;
        snapshot.bytes_captured = task->bytes_captured;
        snapshot.error_message = task->error_message;
        snapshot.client_ip = task->client_ip;
        snapshot.request_user = task->request_user;
        snapshot.captured_files = task->captured_files;
        snapshot.archives = task->archives;

        return true;
    }

    bool query_task_by_key(const std::string& key, TaskSnapshot& snapshot) const
    {
        std::map<std::string, int>::const_iterator it = key_to_id.find(key);
        if (it == key_to_id.end()) {
            return false;
        }
        return query_task(it->second, snapshot);
    }

    bool query_task_by_signature(const std::string& signature, TaskSnapshot& snapshot) const
    {
        std::map<std::string, int>::const_iterator it = signature_to_id.find(signature);
        if (it == signature_to_id.end()) {
            return false;
        }
        return query_task(it->second, snapshot);
    }

    bool query_task_by_sid(const std::string& sid, TaskSnapshot& snapshot) const
    {
        std::map<std::string, int>::const_iterator it = sid_to_id.find(sid);
        if (it == sid_to_id.end()) {
            return false;
        }
        return query_task(it->second, snapshot);
    }

    static bool is_active_status(ECaptureTaskStatus status)
    {
        return status == STATUS_PENDING ||
               status == STATUS_RESOLVING ||
               status == STATUS_RUNNING;
    }
};

class CRxSafeTaskMgr
{
    struct TaskUpdaterStart {
        TaskUpdaterStart(int64_t ts, pid_t pid, const std::string& file)
            : ts_usec(ts), capture_pid(pid), output_file(file)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            long secs = (ts_usec <= 0) ? 0 : static_cast<long>(ts_usec / 1000000LL);
            task.start_time = secs;
            task.capture_pid = capture_pid;
            if (!output_file.empty()) {
                task.output_file = output_file;
            }
        }

        int64_t ts_usec;
        pid_t capture_pid;
        std::string output_file;
    };

    struct TaskUpdaterProgress {
        TaskUpdaterProgress(unsigned long packets_, unsigned long bytes_, int64_t last_ts)
            : packets(packets_), bytes(bytes_), ts_usec(last_ts)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            if (packets > 0) {
                task.packet_count = packets;
            }
            if (bytes > 0) {
                task.bytes_captured = bytes;
            }
            if (ts_usec > 0) {
                long secs = static_cast<long>(ts_usec / 1000000LL);
                task.end_time = secs;
            }
        }

        unsigned long packets;
        unsigned long bytes;
        int64_t ts_usec;
    };

    struct TaskUpdaterFinished {
        TaskUpdaterFinished(int64_t ts, unsigned long packets_, unsigned long bytes_, const std::string& path)
            : ts_usec(ts), packets(packets_), bytes(bytes_), final_path(path)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            long secs = (ts_usec <= 0) ? 0 : static_cast<long>(ts_usec / 1000000LL);
            task.end_time = secs;
            task.packet_count = packets;
            task.bytes_captured = bytes;
            if (!final_path.empty()) {
                task.output_file = final_path;
            }
        }

        int64_t ts_usec;
        unsigned long packets;
        unsigned long bytes;
        std::string final_path;
    };

    struct TaskUpdaterFailed {
        explicit TaskUpdaterFailed(const std::string& msg)
            : message(msg)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            task.error_message = message;
            task.end_time = static_cast<long>(::time(NULL));
        }

        std::string message;
    };

    struct TaskAppendFiles {
        const std::vector<CaptureFileInfo>& files;
        explicit TaskAppendFiles(const std::vector<CaptureFileInfo>& f)
            : files(f)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            for (size_t i = 0; i < files.size(); ++i) {
                bool found = false;
                for (size_t j = 0; j < task.captured_files.size(); ++j) {
                    if (task.captured_files[j].file_path == files[i].file_path) {
                        task.captured_files[j] = files[i];
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    task.captured_files.push_back(files[i]);
                }
            }
        }
    };

;

    struct TaskArchiveRecorder {
        CaptureArchiveInfo archive;
        explicit TaskArchiveRecorder(const CaptureArchiveInfo& info)
            : archive(info)
        {
        }

        void operator()(SRxCaptureTask& task) const
        {
            bool merged = false;
            for (size_t i = 0; i < task.archives.size(); ++i) {
                if (task.archives[i].archive_path == archive.archive_path && !archive.archive_path.empty()) {
                    task.archives[i] = archive;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                task.archives.push_back(archive);
            }
            for (size_t i = 0; i < archive.files.size(); ++i) {
                const CaptureFileInfo& comp = archive.files[i];
                bool found = false;
                for (size_t j = 0; j < task.captured_files.size(); ++j) {
                    if (task.captured_files[j].file_path == comp.file_path) {
                        task.captured_files[j] = comp;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    task.captured_files.push_back(comp);
                }
            }
        }
    };

public:
    CRxSafeTaskMgr()
        : _curr(0)
        , _total_count(0)
        , _pending_count(0)
        , _resolving_count(0)
        , _running_count(0)
        , _completed_count(0)
        , _failed_count(0)
        , _stopped_count(0)
    {
    }

    ~CRxSafeTaskMgr()
    {

        int curr_idx = __sync_fetch_and_add(&_curr, 0);
        for (std::map<int, CRxTaskSlot>::iterator it = _tables[curr_idx].slots.begin();
             it != _tables[curr_idx].slots.end(); ++it) {
            SRxCaptureTask* task = it->second.exchange(NULL);
            if (task) {
                delete task;
            }
        }

        cleanup_pending_deletes();
    }

    bool query_task(int capture_id, TaskSnapshot& snapshot) const
    {

        int curr_idx = __sync_fetch_and_add(const_cast<volatile int*>(&_curr), 0);

        return _tables[curr_idx].query_task(capture_id, snapshot);
    }

    bool query_task_by_key(const std::string& key, TaskSnapshot& snapshot) const
    {
        int curr_idx = __sync_fetch_and_add(const_cast<volatile int*>(&_curr), 0);
        return _tables[curr_idx].query_task_by_key(key, snapshot);
    }

    bool is_key_active(const std::string& key) const
    {
        TaskSnapshot snapshot;
        if (!query_task_by_key(key, snapshot)) {
            return false;
        }
        return TaskTable::is_active_status(snapshot.status);
    }

    bool query_task_by_signature(const std::string& signature, TaskSnapshot& snapshot) const
    {
        int curr_idx = __sync_fetch_and_add(const_cast<volatile int*>(&_curr), 0);
        return _tables[curr_idx].query_task_by_signature(signature, snapshot);
    }

    bool is_signature_active(const std::string& signature) const
    {
        TaskSnapshot snapshot;
        if (!query_task_by_signature(signature, snapshot)) {
            return false;
        }
        return TaskTable::is_active_status(snapshot.status);
    }

    bool query_active_task_by_signature(const std::string& signature, TaskSnapshot& snapshot) const
    {
        if (!query_task_by_signature(signature, snapshot)) {
            return false;
        }
        if (!TaskTable::is_active_status(snapshot.status)) {
            return false;
        }
        return true;
    }

    bool query_task_by_sid(const std::string& sid, TaskSnapshot& snapshot) const
    {
        int curr_idx = __sync_fetch_and_add(const_cast<volatile int*>(&_curr), 0);
        return _tables[curr_idx].query_task_by_sid(sid, snapshot);
    }

    bool is_sid_active(const std::string& sid) const
    {
        TaskSnapshot snapshot;
        if (!query_task_by_sid(sid, snapshot)) {
            return false;
        }
        return TaskTable::is_active_status(snapshot.status);
    }

    bool query_active_task_by_sid(const std::string& sid, TaskSnapshot& snapshot) const
    {
        if (!query_task_by_sid(sid, snapshot)) {
            return false;
        }
        if (!TaskTable::is_active_status(snapshot.status)) {
            return false;
        }
        return true;
    }

    TaskStats get_stats() const
    {
        TaskStats stats;
        stats.total_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_total_count), 0);
        stats.pending_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_pending_count), 0);
        stats.resolving_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_resolving_count), 0);
        stats.running_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_running_count), 0);
        stats.completed_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_completed_count), 0);
        stats.failed_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_failed_count), 0);
        stats.stopped_count = __sync_fetch_and_add(const_cast<volatile size_t*>(&_stopped_count), 0);
        return stats;
    }

    bool set_capture_started(int capture_id, int64_t start_ts_usec,
                             pid_t capture_pid, const std::string& output_file)
    {
        TaskUpdaterStart updater(start_ts_usec, capture_pid, output_file);

        bool updated = update_task(capture_id, updater);
        if (updated) {
            update_status(capture_id, STATUS_RUNNING);
        }
        return updated;
    }

    bool append_capture_files(int capture_id, const std::vector<CaptureFileInfo>& files)
    {
        if (files.empty()) {
            return false;
        }
        TaskAppendFiles updater(files);
        return update_task(capture_id, updater);
    }

    bool record_archive(int capture_id, const CaptureArchiveInfo& archive)
    {
        TaskArchiveRecorder updater(archive);
        return update_task(capture_id, updater);
    }

    bool update_progress(int capture_id, unsigned long packets,
                         unsigned long bytes, int64_t last_ts_usec)
    {
        TaskUpdaterProgress updater(packets, bytes, last_ts_usec);

        return update_task(capture_id, updater);
    }

    bool set_capture_finished(int capture_id, int64_t finish_ts_usec,
                              unsigned long packets, unsigned long bytes,
                              const std::string& final_path)
    {
        TaskUpdaterFinished updater(finish_ts_usec, packets, bytes, final_path);

        bool updated = update_task(capture_id, updater);
        if (updated) {
            update_status(capture_id, STATUS_COMPLETED);
        }
        return updated;
    }

    bool set_capture_failed(int capture_id, const std::string& error_message)
    {
        TaskUpdaterFailed updater(error_message);

        bool updated = update_task(capture_id, updater);
        if (updated) {
            update_status(capture_id, STATUS_FAILED);
        }
        return updated;
    }

    bool add_task(int capture_id,
                  const std::string& key,
                  const std::string& signature,
                  const std::string& sid,
                  SRxCaptureTask* task)
    {
        if (!task) {
            return false;
        }

        int curr_idx = __sync_fetch_and_add(&_curr, 0);
        int idle_idx = 1 - curr_idx;

        _tables[idle_idx] = _tables[curr_idx];

        std::map<int, CRxTaskSlot>::iterator slot_it = _tables[idle_idx].slots.find(capture_id);
        if (slot_it != _tables[idle_idx].slots.end()) {

            SRxCaptureTask* old_task = slot_it->second.exchange(NULL);
            if (old_task) {
                decrement_status_count(old_task->status);
                _pending_deletes.push_back(old_task);
            }
        }

        std::map<std::string, int>::iterator key_it = _tables[idle_idx].key_to_id.find(key);
        if (key_it != _tables[idle_idx].key_to_id.end() && key_it->second != capture_id) {

            SRxCaptureTask* old_task = _tables[idle_idx].remove_task(key_it->second);
            if (old_task) {
                decrement_status_count(old_task->status);
                _pending_deletes.push_back(old_task);
            }
        }

        if (!signature.empty()) {
            std::map<std::string, int>::iterator sig_it =
                _tables[idle_idx].signature_to_id.find(signature);
            if (sig_it != _tables[idle_idx].signature_to_id.end() && sig_it->second != capture_id) {
                SRxCaptureTask* old_task = _tables[idle_idx].remove_task(sig_it->second);
                if (old_task) {
                    decrement_status_count(old_task->status);
                    _pending_deletes.push_back(old_task);
                }
            }
        }

        if (!sid.empty()) {
            std::map<std::string, int>::iterator sid_it =
                _tables[idle_idx].sid_to_id.find(sid);
            if (sid_it != _tables[idle_idx].sid_to_id.end() && sid_it->second != capture_id) {
                SRxCaptureTask* old_task = _tables[idle_idx].remove_task(sid_it->second);
                if (old_task) {
                    decrement_status_count(old_task->status);
                    _pending_deletes.push_back(old_task);
                }
            }
        }

        _tables[idle_idx].add_task(capture_id, key, signature, sid, task);

        __sync_synchronize();

        __sync_lock_test_and_set(&_curr, idle_idx);

        increment_status_count(task->status);

        return true;
    }

    void remove_task(int capture_id)
    {
        int curr_idx = __sync_fetch_and_add(&_curr, 0);
        int idle_idx = 1 - curr_idx;

        _tables[idle_idx] = _tables[curr_idx];

        SRxCaptureTask* old_task = _tables[idle_idx].remove_task(capture_id);

        __sync_synchronize();

        __sync_lock_test_and_set(&_curr, idle_idx);

        if (old_task) {
            decrement_status_count(old_task->status);
            _pending_deletes.push_back(old_task);
        }
    }

    bool update_status(int capture_id, ECaptureTaskStatus new_status)
    {
        int curr_idx = __sync_fetch_and_add(&_curr, 0);

        std::map<int, CRxTaskSlot>::iterator it = _tables[curr_idx].slots.find(capture_id);
        if (it == _tables[curr_idx].slots.end()) {
            return false;
        }

        SRxCaptureTask* old_task = it->second.get();
        if (!old_task) {
            return false;
        }

        SRxCaptureTask* new_task = new SRxCaptureTask(*old_task);
        new_task->status = new_status;

        if (!new_task->signature.empty()) {
            if (TaskTable::is_active_status(new_status)) {
                _tables[curr_idx].signature_to_id[new_task->signature] = capture_id;
            } else {
                std::map<std::string, int>::iterator sig_it =
                    _tables[curr_idx].signature_to_id.find(new_task->signature);
                if (sig_it != _tables[curr_idx].signature_to_id.end() && sig_it->second == capture_id) {
                    _tables[curr_idx].signature_to_id.erase(sig_it);
                }
            }
        }

        if (!new_task->sid.empty()) {
            _tables[curr_idx].sid_to_id[new_task->sid] = capture_id;
        }

        SRxCaptureTask* replaced = it->second.exchange(new_task);

        if (replaced) {
            decrement_status_count(replaced->status);
            increment_status_count(new_status);
            _pending_deletes.push_back(replaced);
        }

        return true;
    }

    template<typename Updater>
    bool update_task(int capture_id, Updater updater)
    {
        int curr_idx = __sync_fetch_and_add(&_curr, 0);

        std::map<int, CRxTaskSlot>::iterator it = _tables[curr_idx].slots.find(capture_id);
        if (it == _tables[curr_idx].slots.end()) {
            return false;
        }

        SRxCaptureTask* old_task = it->second.get();
        if (!old_task) {
            return false;
        }

        SRxCaptureTask* new_task = new SRxCaptureTask(*old_task);

        ECaptureTaskStatus old_status = new_task->status;

        updater(*new_task);

        if (!new_task->signature.empty()) {
            if (TaskTable::is_active_status(new_task->status)) {
                _tables[curr_idx].signature_to_id[new_task->signature] = capture_id;
            } else {
                std::map<std::string, int>::iterator sig_it =
                    _tables[curr_idx].signature_to_id.find(new_task->signature);
                if (sig_it != _tables[curr_idx].signature_to_id.end() && sig_it->second == capture_id) {
                    _tables[curr_idx].signature_to_id.erase(sig_it);
                }
            }
        }

        if (!new_task->sid.empty()) {
            _tables[curr_idx].sid_to_id[new_task->sid] = capture_id;
        }

        SRxCaptureTask* replaced = it->second.exchange(new_task);

        if (replaced && old_status != new_task->status) {
            decrement_status_count(old_status);
            increment_status_count(new_task->status);
        }

        if (replaced) {
            _pending_deletes.push_back(replaced);
        }

        return true;
    }

    void cleanup_pending_deletes()
    {
        for (size_t i = 0; i < _pending_deletes.size(); ++i) {
            delete _pending_deletes[i];
        }
        _pending_deletes.clear();
    }

    size_t pending_delete_count() const
    {
        return _pending_deletes.size();
    }

private:

    void increment_status_count(ECaptureTaskStatus status)
    {
        __sync_fetch_and_add(&_total_count, 1);

        switch (status) {
            case STATUS_PENDING:
                __sync_fetch_and_add(&_pending_count, 1);
                break;
            case STATUS_RESOLVING:
                __sync_fetch_and_add(&_resolving_count, 1);
                break;
            case STATUS_RUNNING:
                __sync_fetch_and_add(&_running_count, 1);
                break;
            case STATUS_COMPLETED:
                __sync_fetch_and_add(&_completed_count, 1);
                break;
            case STATUS_FAILED:
                __sync_fetch_and_add(&_failed_count, 1);
                break;
            case STATUS_STOPPED:
                __sync_fetch_and_add(&_stopped_count, 1);
                break;
        }
    }

    void decrement_status_count(ECaptureTaskStatus status)
    {
        __sync_fetch_and_sub(&_total_count, 1);

        switch (status) {
            case STATUS_PENDING:
                __sync_fetch_and_sub(&_pending_count, 1);
                break;
            case STATUS_RESOLVING:
                __sync_fetch_and_sub(&_resolving_count, 1);
                break;
            case STATUS_RUNNING:
                __sync_fetch_and_sub(&_running_count, 1);
                break;
            case STATUS_COMPLETED:
                __sync_fetch_and_sub(&_completed_count, 1);
                break;
            case STATUS_FAILED:
                __sync_fetch_and_sub(&_failed_count, 1);
                break;
            case STATUS_STOPPED:
                __sync_fetch_and_sub(&_stopped_count, 1);
                break;
        }
    }

private:
    TaskTable _tables[2];
    volatile int _curr;

    volatile size_t _total_count;
    volatile size_t _pending_count;
    volatile size_t _resolving_count;
    volatile size_t _running_count;
    volatile size_t _completed_count;
    volatile size_t _failed_count;
    volatile size_t _stopped_count;

    std::vector<SRxCaptureTask*> _pending_deletes;
};

#endif
;

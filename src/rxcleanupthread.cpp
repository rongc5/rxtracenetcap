#include "rxcleanupthread.h"
#include "legacy_core.h"
#include "rxprocdata.h"
#include "rxcapturemanagerthread.h"

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <time.h>
#include <sstream>
#include <limits.h>

namespace {

bool ensure_directory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    size_t pos = 0;
    while (pos < path.size()) {
        size_t next = path.find('/', pos);
        std::string current;
        if (next == std::string::npos) {
            current = path.substr(0, path.size());
            pos = path.size();
        } else {
            if (next == 0) {
                pos = next + 1;
                continue;
            }
            current = path.substr(0, next);
            pos = next + 1;
        }
        if (current.empty()) {
            continue;
        }
        struct stat st;
        if (stat(current.c_str(), &st) != 0) {
            if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            return false;
        }
    }
    return true;
}

struct RecordFileEntry {
    std::string path;
    time_t mtime;
};

bool record_entry_less(const RecordFileEntry& a, const RecordFileEntry& b)
{
    return a.mtime < b.mtime;
}

struct ArchiveFileEntry {
    std::string path;
    time_t mtime;
    unsigned long long size;
};

bool archive_entry_less(const ArchiveFileEntry& a, const ArchiveFileEntry& b)
{
    return a.mtime < b.mtime;
}

std::string json_escape_local(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string timestamp_suffix()
{
    char buf[32];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_now);
    return std::string(buf);
}

}

CRxCleanupThread::CRxCleanupThread()
    : cleanup_interval_sec_(CLEANUP_INTERVAL_SEC)
    , pdef_dir_("/tmp/rxtracenetcap_pdef")
    , pdef_ttl_seconds_(24 * 3600L)
    , current_record_size_(0)
    , record_max_size_bytes_(RECORD_MAX_SIZE_BYTES)
    , compress_threshold_bytes_(0)
    , archive_max_total_bytes_(0)
    , archive_retention_seconds_(0)
{
}

CRxCleanupThread::~CRxCleanupThread()
{
}

void CRxCleanupThread::configure(const CRxServerConfig::CleanupConfig& cfg, const CRxServerConfig::StorageConfig& storage_cfg)
{
    config_ = cfg;


    pdef_dir_ = storage_cfg.temp_pdef_dir.empty() ? "/tmp/rxtracenetcap_pdef" : storage_cfg.temp_pdef_dir;
    if (storage_cfg.temp_pdef_ttl_hours > 0) {
        long ttl = static_cast<long>(storage_cfg.temp_pdef_ttl_hours) * 3600L;
        pdef_ttl_seconds_ = ttl < 0 ? 0 : ttl;
    } else {
        pdef_ttl_seconds_ = 0;
    }
    if (cfg.archive_keep_days > 0) {
        long days = cfg.archive_keep_days;
        if (days > LONG_MAX / 86400L) {
            archive_retention_seconds_ = LONG_MAX;
        } else {
            archive_retention_seconds_ = days * 86400L;
        }
    } else {
        archive_retention_seconds_ = 0;
    }

    if (cfg.archive_max_total_size_mb > 0) {
        archive_max_total_bytes_ = static_cast<unsigned long long>(cfg.archive_max_total_size_mb) * 1024ULL * 1024ULL;
    } else {
        archive_max_total_bytes_ = 0;
    }
}

bool CRxCleanupThread::start()
{
    if (!base_net_thread::start()) {
        return false;
    }
    std::string record_dir = get_record_base_dir();
    if (!ensure_directory(record_dir)) {
        LOG_WARNING("Cleanup: failed to ensure record directory %s", record_dir.c_str());
    }
    if (!ensure_directory(config_.archive_dir)) {
        LOG_WARNING("Cleanup: failed to ensure archive directory %s", config_.archive_dir.c_str());
    }
    if (!pdef_dir_.empty() && !ensure_directory(pdef_dir_)) {
        LOG_WARNING("Cleanup: failed to ensure pdef directory %s", pdef_dir_.c_str());
    }
    schedule_cleanup_timer();
    return true;
}

void CRxCleanupThread::handle_msg(shared_ptr<normal_msg>& msg)
{
    if (!msg) {
        return;
    }

    switch (msg->_msg_op) {
        case RX_MSG_FILE_ENQUEUE:
        {
            shared_ptr<SRxFileEnqueueMsgV2> enqueue =
                dynamic_pointer_cast<SRxFileEnqueueMsgV2>(msg);
            if (enqueue) {
                enqueue_files(*enqueue);
            }
            break;
        }
        case RX_MSG_CLEAN_CFG_REFRESH:
        {

            shared_ptr<SRxCleanConfigRefreshMsgV2> cfg =
                dynamic_pointer_cast<SRxCleanConfigRefreshMsgV2>(msg);
            if (cfg) {
                LOG_NOTICE("Cleanup thread received config refresh (hash=%u)",
                           cfg->config_hash);
            }
            break;
        }
        case RX_MSG_CLEAN_SHUTDOWN:
        {
            LOG_NOTICE("Cleanup thread received shutdown request");
            set_run_flag(false);
            break;
        }
        default:
            base_net_thread::handle_msg(msg);
            break;
    }
}

void CRxCleanupThread::handle_timeout(shared_ptr<timer_msg>& t_msg)
{
    if (!t_msg) {
        return;
    }
    if (t_msg->_timer_type != CLEANUP_TIMER_TYPE) {
        base_net_thread::handle_timeout(t_msg);
        return;
    }
    do_cleanup();
    if (get_run_flag()) {
        schedule_cleanup_timer();
    }
}

void CRxCleanupThread::schedule_cleanup_timer()
{
    shared_ptr<timer_msg> t_msg(new timer_msg);
    t_msg->_obj_id = OBJ_ID_THREAD;
    t_msg->_timer_type = CLEANUP_TIMER_TYPE;
    t_msg->_time_length = static_cast<uint32_t>(cleanup_interval_sec_ * 1000);
    add_timer(t_msg);
}

void CRxCleanupThread::do_cleanup()
{
    cleanup_pdef_temp_files();
    process_pending_files();
    prune_archives();
}

void CRxCleanupThread::cleanup_pdef_temp_files()
{
    if (pdef_dir_.empty() || pdef_ttl_seconds_ <= 0) {
        return;
    }

    DIR* dir = opendir(pdef_dir_.c_str());
    if (!dir) {
        return;
    }

    time_t now = time(NULL);
    size_t removed = 0;
    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        std::string path = pdef_dir_;
        if (!path.empty() && path[path.size() - 1] != '/') {
            path += "/";
        }
        path += ent->d_name;

        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (now - st.st_mtime >= pdef_ttl_seconds_) {
            if (::remove(path.c_str()) == 0) {
                removed++;
                LOG_NOTICE("Cleanup: removed expired PDEF temp file %s", path.c_str());
            } else {
                LOG_WARNING("Cleanup: failed to remove PDEF temp file %s (errno=%d)", path.c_str(), errno);
            }
        }
    }
    closedir(dir);

    if (removed > 0) {
        LOG_NOTICE("Cleanup: removed %zu expired PDEF temp file(s)", removed);
    }
}

void CRxCleanupThread::enqueue_files(const SRxFileEnqueueMsgV2& enqueue_msg)
{
    if (enqueue_msg.files.empty()) {
        return;
    }

    for (size_t i = 0; i < enqueue_msg.files.size(); ++i) {
        PendingFile pending;
        pending.capture_id = enqueue_msg.capture_id;
        pending.key = enqueue_msg.key;
        pending.sid = enqueue_msg.sid;
        CaptureFileInfo file_info = enqueue_msg.files[i];
        std::string record_path = record_file_metadata(enqueue_msg.capture_id,
                                                       enqueue_msg.key,
                                                       enqueue_msg.files[i]);
        if (!record_path.empty()) {
            file_info.record_path = record_path;
        }
        pending.file = file_info;
        pending.policy = enqueue_msg.clean_policy;
        pending_files_.push_back(pending);

        CRxProcData* global = CRxProcData::instance();
        if (global) {
            std::vector<CaptureFileInfo> files;
            files.push_back(file_info);
            global->capture_task_mgr().append_capture_files(enqueue_msg.capture_id, files);
        }
    }

    LOG_NOTICE("Cleanup thread queued %zu file(s) for capture %d (total pending=%zu)",
               enqueue_msg.files.size(),
               enqueue_msg.capture_id,
               pending_files_.size());
}

void CRxCleanupThread::process_pending_files()
{
    if (pending_files_.empty()) {
        return;
    }


    size_t total_count = pending_files_.size();
    unsigned long long total_size = 0;
    for (size_t i = 0; i < pending_files_.size(); ++i) {
        struct stat st;
        if (stat(pending_files_[i].file.file_path.c_str(), &st) == 0) {
            total_size += static_cast<unsigned long long>(st.st_size);
        }
    }


    unsigned long long threshold_bytes = static_cast<unsigned long long>(config_.batch_compress_size_mb) * 1024ULL * 1024ULL;
    bool should_compress = (total_count >= config_.batch_compress_file_count) ||
                          (total_size >= threshold_bytes);

    if (!should_compress) {
        LOG_DEBUG("Cleanup: batch compression threshold not met (count=%zu/%u, size=%llu/%llu MB)",
                  total_count, config_.batch_compress_file_count,
                  total_size / (1024ULL * 1024ULL),
                  static_cast<unsigned long long>(config_.batch_compress_size_mb));
        return;
    }


    LOG_NOTICE("Cleanup: starting batch compression for %zu files (total size=%llu MB)",
               total_count, total_size / (1024ULL * 1024ULL));


    std::map<int, std::vector<PendingFile> > groups;
    for (size_t i = 0; i < pending_files_.size(); ++i) {
        groups[pending_files_[i].capture_id].push_back(pending_files_[i]);
    }


    for (std::map<int, std::vector<PendingFile> >::iterator it = groups.begin();
         it != groups.end(); ++it) {
        CaptureArchiveInfo archive;
        std::string error;
        if (compress_batch(it->second, archive, error)) {
            if (!archive.files.empty() && !it->second.empty()) {
                notify_archive_result(it->second[0].capture_id,
                                    it->second[0].key,
                                    it->second[0].sid,
                                    archive);
            }
        } else {
            if (!it->second.empty()) {
                std::vector<CaptureFileInfo> failed;
                for (size_t i = 0; i < it->second.size(); ++i) {
                    failed.push_back(it->second[i].file);
                }
                notify_archive_failure(it->second[0].capture_id,
                                     it->second[0].key,
                                     it->second[0].sid,
                                     failed, error);
            }
        }
    }


    pending_files_.clear();
}

bool CRxCleanupThread::compress_file(const PendingFile& pending,
                                     CaptureArchiveInfo& archive,
                                     std::string& error_msg)
{
    struct stat st;
    if (stat(pending.file.file_path.c_str(), &st) != 0) {
        LOG_WARNING("Cleanup: file %s not found, skipping", pending.file.file_path.c_str());
        error_msg = "file_not_found";
        return false;
    }

    if (!pending.policy.compress_enabled) {
        LOG_DEBUG("Cleanup: compression disabled, leaving file %s as-is",
                  pending.file.file_path.c_str());
        return true;
    }

    if (!should_compress_size(static_cast<unsigned long>(st.st_size),
                              pending.policy.compress_threshold_mb)) {
        LOG_NOTICE("Cleanup: file %s (%lu bytes) below compression threshold, skipping",
                   pending.file.file_path.c_str(),
                   static_cast<unsigned long>(st.st_size));
        return true;
    }

    bool use_policy_cmd = !pending.policy.compress_format.empty();
    std::string archive_path;
    std::string command;

    if (use_policy_cmd) {
        command = pending.policy.compress_format;
        command += " '";
        command += pending.file.file_path;
        command += "'";
    } else {
        if (!ensure_directory(config_.archive_dir)) {
            LOG_WARNING("Cleanup: archive directory %s unavailable", config_.archive_dir.c_str());
            error_msg = "archive_dir_unavailable";
            return false;
        }
        archive_path = config_.archive_dir;
        if (!archive_path.empty() && archive_path[archive_path.size() - 1] != '/') {
            archive_path += "/";
        }
        archive_path += "capture_";
        archive_path += timestamp_suffix();
        archive_path += "_";
        char seqbuf[32];
        snprintf(seqbuf, sizeof(seqbuf), "%d_%d", pending.capture_id, pending.file.segment_index);
        archive_path += seqbuf;
        archive_path += ".tgz";

        command = "tar -czf '";
        command += archive_path;
        command += "' '";
        command += pending.file.file_path;
        command += "'";
    }

    LOG_NOTICE("Cleanup: compressing %s with command: %s", pending.file.file_path.c_str(), command.c_str());
    int rc = std::system(command.c_str());
    if (rc != 0) {
        LOG_WARNING("Cleanup: compression command failed rc=%d for %s",
                    rc, pending.file.file_path.c_str());
        error_msg = "compress_failed";
        return false;
    }

    std::string produced_path;
    unsigned long archive_size = 0;
    if (use_policy_cmd) {
        struct stat after;
        if (stat(pending.file.file_path.c_str(), &after) == 0) {
            produced_path = pending.file.file_path;
            archive_size = static_cast<unsigned long>(after.st_size);
        } else {
            std::string gz_path = pending.file.file_path;
            gz_path += ".gz";
            if (stat(gz_path.c_str(), &after) == 0) {
                produced_path = gz_path;
                archive_size = static_cast<unsigned long>(after.st_size);
            } else {
                produced_path = pending.file.file_path;
            }
        }
    } else {
        struct stat after;
        if (stat(archive_path.c_str(), &after) == 0) {
            archive_size = static_cast<unsigned long>(after.st_size);
        }
        produced_path = archive_path;
    }

    if ((config_.archive_remove_source || pending.policy.compress_remove_src) && stat(pending.file.file_path.c_str(), &st) == 0) {
        if (::remove(pending.file.file_path.c_str()) != 0) {
            LOG_WARNING("Cleanup: failed to remove source file %s", pending.file.file_path.c_str());
        }
    }

    CaptureFileInfo compressed = pending.file;
    compressed.compressed = true;
    compressed.archive_path = produced_path.empty() ? pending.file.file_path : produced_path;
    compressed.compress_finish_ts = static_cast<int64_t>(time(NULL));

    archive.archive_path = compressed.archive_path;
    archive.archive_size = archive_size;
    archive.compress_finish_ts = compressed.compress_finish_ts;
    archive.files.clear();
    archive.files.push_back(compressed);

    LOG_NOTICE("Cleanup: compression complete for %s (archive=%s size=%lu)",
               pending.file.file_path.c_str(),
               compressed.archive_path.c_str(),
               archive_size);
    return true;
}

bool CRxCleanupThread::compress_batch(const std::vector<PendingFile>& files,
                                       CaptureArchiveInfo& archive,
                                       std::string& error_msg)
{
    if (files.empty()) {
        error_msg = "no_files_to_compress";
        return false;
    }


    if (!ensure_directory(config_.archive_dir)) {
        LOG_WARNING("Cleanup: archive directory %s unavailable", config_.archive_dir.c_str());
        error_msg = "archive_dir_unavailable";
        return false;
    }


    std::string archive_path = config_.archive_dir;
    if (!archive_path.empty() && archive_path[archive_path.size() - 1] != '/') {
        archive_path += "/";
    }
    archive_path += "batch_";
    archive_path += timestamp_suffix();
    archive_path += "_";
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", files[0].capture_id);
    archive_path += buf;
    archive_path += ".tar.gz";


    std::string command = "tar -czf '";
    command += archive_path;
    command += "'";
    size_t file_count = 0;
    for (size_t i = 0; i < files.size(); ++i) {

        std::string filename = files[i].file.file_path;
        size_t pos = filename.find_last_of('/');
        std::string basename = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
        if (basename.find("cleanup") == 0 && basename.find(".log") != std::string::npos) {
            LOG_DEBUG("Cleanup: skipping record file %s from compression", filename.c_str());
            continue;
        }
        command += " '";
        command += files[i].file.file_path;
        command += "'";
        file_count++;
    }

    if (file_count == 0) {
        LOG_WARNING("Cleanup: no files to compress after filtering");
        error_msg = "no_files_after_filter";
        return false;
    }

    LOG_NOTICE("Cleanup: batch compressing %zu files into %s", files.size(), archive_path.c_str());
    int rc = std::system(command.c_str());
    if (rc != 0) {
        LOG_WARNING("Cleanup: batch compression command failed rc=%d", rc);
        error_msg = "compress_failed";
        return false;
    }


    struct stat archive_st;
    unsigned long archive_size = 0;
    if (stat(archive_path.c_str(), &archive_st) == 0) {
        archive_size = static_cast<unsigned long>(archive_st.st_size);
    }


    if (config_.archive_remove_source) {
        for (size_t i = 0; i < files.size(); ++i) {

            std::string filename = files[i].file.file_path;
            size_t pos = filename.find_last_of('/');
            std::string basename = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
            if (basename.find("cleanup") == 0 && basename.find(".log") != std::string::npos) {
                LOG_DEBUG("Cleanup: skipping record file %s from removal", filename.c_str());
                continue;
            }
            if (::remove(files[i].file.file_path.c_str()) != 0) {
                LOG_WARNING("Cleanup: failed to remove source file %s", files[i].file.file_path.c_str());
            }
        }
    }


    archive.archive_path = archive_path;
    archive.archive_size = archive_size;
    archive.compress_finish_ts = static_cast<int64_t>(time(NULL));
    archive.files.clear();

    for (size_t i = 0; i < files.size(); ++i) {
        CaptureFileInfo compressed = files[i].file;
        compressed.compressed = true;
        compressed.archive_path = archive_path;
        compressed.compress_finish_ts = archive.compress_finish_ts;
        archive.files.push_back(compressed);
    }

    LOG_NOTICE("Cleanup: batch compression complete, %zu files -> %s (size=%lu)",
               files.size(), archive_path.c_str(), archive_size);
    return true;
}

std::string CRxCleanupThread::get_record_base_dir() const
{

    std::string base;
    CRxProcData* global = CRxProcData::instance();
    if (global) {
        CRxStrategyConfigManager* cfg = global->current_strategy_config();
        if (cfg) {
            base = cfg->storage().base_dir;
        }
    }
    if (base.empty()) {
        base = "/var/log/rxtrace/captures";
    }
    return base;
}

std::string CRxCleanupThread::current_record_file() const
{
    std::string base = get_record_base_dir();
    if (!base.empty() && base[base.size() - 1] != '/') {
        base += "/";
    }
    base += "cleanup.log";
    return base;
}

void CRxCleanupThread::rotate_record_file_if_needed(size_t incoming_size)
{
    if (record_file_path_.empty()) {
        record_file_path_ = current_record_file();
        struct stat st;
        if (stat(record_file_path_.c_str(), &st) == 0) {
            current_record_size_ = static_cast<unsigned long>(st.st_size);
        } else {
            current_record_size_ = 0;
        }
    }

    if (record_max_size_bytes_ == 0) {
        return;
    }

    if (current_record_size_ + incoming_size <= record_max_size_bytes_) {
        return;
    }

    std::string rotated = get_record_base_dir();
    if (!rotated.empty() && rotated[rotated.size() - 1] != '/') {
        rotated += "/";
    }
    rotated += "cleanup_";
    rotated += timestamp_suffix();
    rotated += ".log";

    if (::rename(record_file_path_.c_str(), rotated.c_str()) != 0) {
        LOG_WARNING("Cleanup: failed to rotate record file %s -> %s (errno=%d)",
                    record_file_path_.c_str(), rotated.c_str(), errno);
        current_record_size_ = 0;
    } else {
        LOG_NOTICE("Cleanup: rotated record file to %s", rotated.c_str());
        current_record_size_ = 0;
        prune_record_files();
    }
}

void CRxCleanupThread::prune_record_files()
{
    std::string record_dir = get_record_base_dir();
    DIR* dir = opendir(record_dir.c_str());
    if (!dir) {
        return;
    }

    std::vector<RecordFileEntry> entries;
    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        std::string name(ent->d_name);
        if (name.find("cleanup") != 0) {
            continue;
        }
        std::string path = record_dir;
        if (!path.empty() && path[path.size() - 1] != '/') {
            path += "/";
        }
        path += name;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            RecordFileEntry entry;
            entry.path = path;
            entry.mtime = st.st_mtime;
            entries.push_back(entry);
        }
    }
    closedir(dir);

    if (entries.size() <= RECORD_MAX_FILES) {
        return;
    }

    std::sort(entries.begin(), entries.end(), record_entry_less);

    size_t to_remove = entries.size() - RECORD_MAX_FILES;
    for (size_t i = 0; i < to_remove; ++i) {
        if (::remove(entries[i].path.c_str()) == 0) {
            LOG_NOTICE("Cleanup: pruned old record file %s", entries[i].path.c_str());
        }
    }
}

std::string CRxCleanupThread::record_file_metadata(int capture_id,
                                                   const std::string& key,
                                                   const CaptureFileInfo& info)
{
    std::ostringstream oss;
    int64_t ts = static_cast<int64_t>(time(NULL));
    oss << "{\"ts\":" << ts
        << ",\"capture_id\":" << capture_id
        << ",\"key\":\"" << json_escape_local(key) << "\""
        << ",\"file\":\"" << json_escape_local(info.file_path) << "\""
        << ",\"size\":" << info.file_size
        << ",\"segment\":" << info.segment_index
        << ",\"segments\":" << info.total_segments
        << "}\n";

    std::string line = oss.str();
    std::string record_dir = get_record_base_dir();
    if (!ensure_directory(record_dir)) {
        LOG_WARNING("Cleanup: record directory %s unavailable", record_dir.c_str());
        return std::string();
    }

    if (record_file_path_.empty()) {
        record_file_path_ = current_record_file();
        struct stat st;
        if (stat(record_file_path_.c_str(), &st) == 0) {
            current_record_size_ = static_cast<unsigned long>(st.st_size);
        } else {
            current_record_size_ = 0;
        }
    }

    rotate_record_file_if_needed(line.size());

    FILE* fp = ::fopen(record_file_path_.c_str(), "a");
    if (!fp) {
        LOG_WARNING("Cleanup: failed to open record file %s", record_file_path_.c_str());
        return std::string();
    }
    size_t written = ::fwrite(line.data(), 1, line.size(), fp);
    ::fclose(fp);
    if (written != line.size()) {
        LOG_WARNING("Cleanup: failed to write full record entry to %s", record_file_path_.c_str());
    } else {
        current_record_size_ += static_cast<unsigned long>(written);
    }
    return record_file_path_;
}

bool CRxCleanupThread::should_compress_size(unsigned long file_size, int policy_threshold_mb) const
{
    unsigned long effective = compress_threshold_bytes_;
    if (policy_threshold_mb > 0) {
        unsigned long policy_bytes = static_cast<unsigned long>(policy_threshold_mb) * 1024UL * 1024UL;
        effective = policy_bytes;
    }
    if (effective == 0) {
        return true;
    }
    return file_size >= effective;
}

void CRxCleanupThread::prune_archives()
{
    if (config_.archive_dir.empty()) {
        return;
    }
    if (archive_retention_seconds_ <= 0 && archive_max_total_bytes_ == 0) {
        return;
    }

    DIR* dir = opendir(config_.archive_dir.c_str());
    if (!dir) {
        return;
    }

    time_t now = time(NULL);
    const bool enforce_retention = (archive_retention_seconds_ > 0);
    const bool enforce_size = (archive_max_total_bytes_ > 0);

    time_t cutoff = 0;
    if (enforce_retention) {
        if (archive_retention_seconds_ < 0 || archive_retention_seconds_ > now) {
            cutoff = 0;
        } else {
            cutoff = now - static_cast<time_t>(archive_retention_seconds_);
        }
    }

    std::string base = config_.archive_dir;
    if (!base.empty() && base[base.size() - 1] != '/') {
        base += "/";
    }

    std::vector<ArchiveFileEntry> entries;
    unsigned long long total_size = 0;

    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        std::string name(ent->d_name);
        if (name.find("capture_") != 0) {
            continue;
        }
        std::string path = base + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (enforce_retention && cutoff != 0 && st.st_mtime < cutoff) {
            if (::remove(path.c_str()) == 0) {
                LOG_NOTICE("Cleanup: pruned archive file %s (age=%ld)", path.c_str(),
                           static_cast<long>(now - st.st_mtime));
            } else {
                LOG_WARNING("Cleanup: failed to prune archive file %s", path.c_str());
            }
            continue;
        }

        if (!enforce_size) {
            continue;
        }

        ArchiveFileEntry entry;
        entry.path = path;
        entry.mtime = st.st_mtime;
        entry.size = static_cast<unsigned long long>(st.st_size);
        entries.push_back(entry);
        total_size += entry.size;
    }
    closedir(dir);

    if (!enforce_size || entries.empty()) {
        return;
    }

    if (total_size <= archive_max_total_bytes_) {
        return;
    }

    std::sort(entries.begin(), entries.end(), archive_entry_less);

    for (size_t i = 0; i < entries.size() && total_size > archive_max_total_bytes_; ++i) {
        const ArchiveFileEntry& entry = entries[i];
        if (::remove(entry.path.c_str()) == 0) {
            if (entry.size <= total_size) {
                total_size -= entry.size;
            } else {
                total_size = 0;
            }
            long age = static_cast<long>(now - entry.mtime);
            LOG_NOTICE("Cleanup: pruned archive file %s due to size limit (age=%ld, remaining_bytes=%llu)",
                       entry.path.c_str(),
                       age,
                       static_cast<unsigned long long>(total_size));
        } else {
            LOG_WARNING("Cleanup: failed to prune archive file %s due to size limit",
                        entry.path.c_str());
        }
    }
}

void CRxCleanupThread::notify_archive_result(int capture_id,
                                             const std::string& key,
                                             const std::string& sid,
                                             const CaptureArchiveInfo& archive)
{
    CRxProcData* global = CRxProcData::instance();
    if (!global) {
        return;
    }

    CRxCaptureManagerThread* manager = global->get_capture_manager_thread();
    if (!manager) {
        return;
    }

    shared_ptr<SRxCleanCompressDoneMsgV2> msg(new SRxCleanCompressDoneMsgV2());
    msg->capture_id = capture_id;
    msg->key = key;
    msg->sid = sid;
    msg->compressed_files = archive.files;
    msg->archive_path = archive.archive_path;
    msg->compressed_bytes = archive.archive_size;
    msg->compress_duration_ms = 0;
    msg->sender_thread_index = static_cast<int>(get_thread_index());

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = manager->get_thread_index();
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(msg);
    base_net_thread::put_obj_msg(target, base);
}

void CRxCleanupThread::notify_archive_failure(int capture_id,
                                              const std::string& key,
                                              const std::string& sid,
                                              const std::vector<CaptureFileInfo>& files,
                                              const std::string& error)
{
    CRxProcData* global = CRxProcData::instance();
    if (!global) {
        return;
    }

    CRxCaptureManagerThread* manager = global->get_capture_manager_thread();
    if (!manager) {
        return;
    }

    shared_ptr<SRxCleanCompressFailedMsgV2> msg(new SRxCleanCompressFailedMsgV2());
    msg->capture_id = capture_id;
    msg->key = key;
    msg->sid = sid;
    msg->failed_files = files;
    msg->error_code = ERR_CLEAN_COMPRESS_FAILED;
    msg->error_message = error;
    msg->sender_thread_index = static_cast<int>(get_thread_index());

    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = manager->get_thread_index();
    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(msg);
    base_net_thread::put_obj_msg(target, base);
}

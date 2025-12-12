#ifndef RX_SERVER_CONFIG_H
#define RX_SERVER_CONFIG_H

#include <string>
#include "legacy_core.h"

class CRxServerConfig {
public:
    CRxServerConfig();
    explicit CRxServerConfig(const char* argv0);
    bool load_from_process(const char* argv0);

    struct LogConfig {
        std::string path;
        std::string prefix;
        unsigned int size_mb;
        int level;

        LogConfig()
            : path("logs/rxtracenetcap.log")
            , prefix("rxtrace")
            , size_mb(100)
            , level(LOG_LEVEL_DEBUG)
        {
        }
    } log_config;

    struct CaptureConfig {
        std::string default_interface;
        int default_duration;
        std::string default_category;
        std::string file_pattern;
        long max_file_size_mb;

        CaptureConfig()
            : default_interface("any")
            , default_duration(60)
            , default_category("diag")
            , file_pattern("{day}/{date}-{iface}-{proc}-{port}.pcap")
            , max_file_size_mb(200)
        {
        }
    } capture_config;

    struct StorageConfig {
        std::string base_dir;
        int max_age_days;
        long max_size_gb;
        std::string temp_pdef_dir;
        int temp_pdef_ttl_hours;

        StorageConfig()
            : base_dir("/var/log/rxtrace/captures")
            , max_age_days(7)
            , max_size_gb(100)
            , temp_pdef_dir("/tmp/rxtracenetcap_pdef")
            , temp_pdef_ttl_hours(24)
        {
        }
    } storage_config;

    struct CleanupConfig {
        int compress_interval_sec;
        unsigned int batch_compress_file_count;
        unsigned long batch_compress_size_mb;
        std::string archive_dir;
        int archive_keep_days;
        unsigned long archive_max_total_size_mb;
        bool archive_remove_source;

        CleanupConfig()
            : compress_interval_sec(600)
            , batch_compress_file_count(10)
            , batch_compress_size_mb(1024)
            , archive_dir("/var/log/rxtrace/archives")
            , archive_keep_days(14)
            , archive_max_total_size_mb(0)
            , archive_remove_source(true)
        {
        }
    } cleanup_config;

    struct LimitsConfig {
        int max_concurrent_captures;

        LimitsConfig()
            : max_concurrent_captures(8)
        {
        }
    } limits_config;

    std::string log_path;

    const std::string& bind_addr() const { return bind_addr_; }
    int port() const { return port_; }
    int workers() const { return workers_; }
    int capture_threads() const { return capture_threads_; }
    const std::string& strategy_path() const { return strategy_path_; }
    const std::string& loaded_path() const { return loaded_path_; }
    const CaptureConfig& capture() const { return capture_config; }
    const StorageConfig& storage() const { return storage_config; }
    const CleanupConfig& cleanup() const { return cleanup_config; }
    const LimitsConfig& limits() const { return limits_config; }

private:
    static std::string deduce_path_from_argv(const char* argv0);
    bool load_from_path(const std::string& path);
    void init_defaults();
    void update_log_path();

private:
    std::string bind_addr_;
    int port_;
    int workers_;
    int capture_threads_;
    std::string strategy_path_;
    std::string loaded_path_;
};

#endif

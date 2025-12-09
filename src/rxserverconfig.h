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

    struct CleanupConfig {
        std::string record_dir;
        unsigned int record_max_size_mb;
        unsigned int record_max_files;
        int compress_interval_sec;
        unsigned long compress_threshold_mb;
        std::string archive_dir;
        std::string archive_format;
        int archive_keep_days;
        unsigned long archive_max_total_size_mb;
        bool archive_remove_source;
        std::string pdef_dir;
        int pdef_ttl_hours;

        CleanupConfig()
            : record_dir("/var/log/rxtrace/cleanup")
            , record_max_size_mb(50)
            , record_max_files(5)
            , compress_interval_sec(600)
            , compress_threshold_mb(1024)
            , archive_dir("/var/log/rxtrace/archives")
            , archive_format("tar.gz")
            , archive_keep_days(14)
            , archive_max_total_size_mb(0)
            , archive_remove_source(true)
            , pdef_dir("/tmp/rxtracenetcap_pdef")
            , pdef_ttl_hours(24)
        {
        }
    } cleanup_config;

    std::string log_path;

    const std::string& bind_addr() const { return bind_addr_; }
    int port() const { return port_; }
    int workers() const { return workers_; }
    int capture_threads() const { return capture_threads_; }
    const std::string& strategy_path() const { return strategy_path_; }
    const std::string& loaded_path() const { return loaded_path_; }
    const CleanupConfig& cleanup() const { return cleanup_config; }

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

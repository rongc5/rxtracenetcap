#include "rxserverconfig.h"
#include "legacy_core.h"

#include <string.h>
#include <stdio.h>

CRxServerConfig::CRxServerConfig()
{
    init_defaults();
}

CRxServerConfig::CRxServerConfig(const char* argv0)
{
    init_defaults();
    if (argv0 && load_from_path(argv0)) {
        return;
    }
    load_from_process(argv0);
}

void CRxServerConfig::init_defaults()
{
    bind_addr_ = "0.0.0.0";
    port_ = 8080;
    workers_ = 2;
    capture_threads_ = 4;
    strategy_path_.clear();
    loaded_path_.clear();
    log_config = LogConfig();
    cleanup_config = CleanupConfig();
    update_log_path();
}

std::string CRxServerConfig::deduce_path_from_argv(const char* argv0)
{
    const char* last_slash = argv0 ? strrchr(argv0, '/') : 0;
    const char* base = last_slash ? last_slash + 1 : (argv0 ? argv0 : "rxtracenetcap");
    std::string base_name = (base && *base) ? std::string(base) : std::string("rxtracenetcap");
    size_t dot = base_name.find_last_of('.');
    if (dot != std::string::npos) {
        base_name = base_name.substr(0, dot);
    }
    if (base_name.empty()) {
        base_name = "rxtracenetcap";
    }
    std::string path = std::string("config/") + base_name + ".ini";
    return path;
}

bool CRxServerConfig::load_from_path(const std::string& path)
{
    CRxIniConfig ini;
    if (!ini.load(path)) {
        return false;
    }
    bind_addr_ = ini.value("server", "bind_addr", bind_addr_);
    port_ = ini.intValue("server", "port", port_);
    workers_ = ini.intValue("server", "workers", workers_);
    capture_threads_ = ini.intValue("server", "capture_threads", capture_threads_);
    strategy_path_ = ini.value("server", "strategy_path", strategy_path_);
    log_config.path = ini.value("logging", "log_path", log_config.path);
    log_config.prefix = ini.value("logging", "log_prefix", log_config.prefix);
    log_config.size_mb = ini.intValue("logging", "log_size_mb", log_config.size_mb);
    log_config.level = ini.intValue("logging", "log_level", log_config.level);
    cleanup_config.record_dir = ini.value("cleanup", "record_dir", cleanup_config.record_dir);
    cleanup_config.record_max_size_mb = ini.intValue("cleanup", "record_max_size_mb", cleanup_config.record_max_size_mb);
    cleanup_config.record_max_files = ini.intValue("cleanup", "record_max_files", cleanup_config.record_max_files);
    cleanup_config.compress_interval_sec = ini.intValue("cleanup", "compress_interval_sec", cleanup_config.compress_interval_sec);
    cleanup_config.compress_threshold_mb = ini.intValue("cleanup", "compress_threshold_mb", static_cast<int>(cleanup_config.compress_threshold_mb));
    cleanup_config.archive_dir = ini.value("cleanup", "archive_dir", cleanup_config.archive_dir);
    cleanup_config.archive_format = ini.value("cleanup", "archive_format", cleanup_config.archive_format);
    cleanup_config.archive_keep_days = ini.intValue("cleanup", "archive_keep_days", cleanup_config.archive_keep_days);
    cleanup_config.archive_max_total_size_mb = static_cast<unsigned long>(
        ini.intValue("cleanup",
                     "archive_max_total_size_mb",
                     static_cast<int>(cleanup_config.archive_max_total_size_mb)));
    cleanup_config.archive_remove_source = ini.boolValue("cleanup", "archive_remove_source", cleanup_config.archive_remove_source);
    cleanup_config.pdef_dir = ini.value("cleanup", "pdef_dir", cleanup_config.pdef_dir);
    cleanup_config.pdef_ttl_hours = ini.intValue("cleanup", "pdef_ttl_hours", cleanup_config.pdef_ttl_hours);

    loaded_path_ = path;
    update_log_path();
    return true;
}

bool CRxServerConfig::load_from_process(const char* argv0)
{
    std::string path = deduce_path_from_argv(argv0);
    if (load_from_path(path)) {
        return true;
    }
    return load_from_path("../config/strategy.ini");
}

void CRxServerConfig::update_log_path()
{
    std::string config = log_config.path.empty() ? std::string("logs/rxtracenetcap.log") : log_config.path;
    bool has_query = (config.find('?') != std::string::npos);

    std::string options;
    const std::string amp = has_query ? "&" : "?";

    if (!log_config.prefix.empty()) {
        options += (options.empty() ? amp : "&");
        options += "prefix=" + log_config.prefix;
    }
    if (log_config.size_mb > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u", log_config.size_mb);
        options += (options.empty() ? amp : "&");
        options += "size_mb=";
        options += buf;
    }
    if (log_config.level > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", log_config.level);
        options += (options.empty() ? amp : "&");
        options += "level=";
        options += buf;
    }

    if (!options.empty()) {
        config += options;
    }

    log_path = config;
}

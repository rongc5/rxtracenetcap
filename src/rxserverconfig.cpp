#include "rxserverconfig.h"
#include "legacy_core.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"

#include <string.h>
#include <stdio.h>
#include <fstream>
#include <sstream>

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
    strategy_path_ = "config/strategy.json";
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
    std::string path = std::string("config/") + base_name + ".json";
    return path;
}

bool CRxServerConfig::load_from_path(const std::string& path)
{

    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    ifs.close();


    rapidjson::Document doc;
    doc.Parse(content.c_str());

    if (doc.HasParseError()) {
        return false;
    }

    if (!doc.IsObject()) {
        return false;
    }


    if (doc.HasMember("server") && doc["server"].IsObject()) {
        const rapidjson::Value& server = doc["server"];
        if (server.HasMember("bind_addr") && server["bind_addr"].IsString()) {
            bind_addr_ = server["bind_addr"].GetString();
        }
        if (server.HasMember("port") && server["port"].IsInt()) {
            port_ = server["port"].GetInt();
        }
        if (server.HasMember("workers") && server["workers"].IsInt()) {
            workers_ = server["workers"].GetInt();
        }
        if (server.HasMember("capture_threads") && server["capture_threads"].IsInt()) {
            capture_threads_ = server["capture_threads"].GetInt();
        }
    }


    if (doc.HasMember("logging") && doc["logging"].IsObject()) {
        const rapidjson::Value& logging = doc["logging"];
        if (logging.HasMember("log_path") && logging["log_path"].IsString()) {
            log_config.path = logging["log_path"].GetString();
        }
        if (logging.HasMember("log_prefix") && logging["log_prefix"].IsString()) {
            log_config.prefix = logging["log_prefix"].GetString();
        }
        if (logging.HasMember("log_size_mb") && logging["log_size_mb"].IsInt()) {
            log_config.size_mb = logging["log_size_mb"].GetInt();
        }
        if (logging.HasMember("log_level") && logging["log_level"].IsInt()) {
            log_config.level = logging["log_level"].GetInt();
        }
    }


    if (doc.HasMember("capture") && doc["capture"].IsObject()) {
        const rapidjson::Value& capture = doc["capture"];
        if (capture.HasMember("default_interface") && capture["default_interface"].IsString()) {
            capture_config.default_interface = capture["default_interface"].GetString();
        }
        if (capture.HasMember("default_duration") && capture["default_duration"].IsInt()) {
            capture_config.default_duration = capture["default_duration"].GetInt();
        }
        if (capture.HasMember("default_category") && capture["default_category"].IsString()) {
            capture_config.default_category = capture["default_category"].GetString();
        }
        if (capture.HasMember("file_pattern") && capture["file_pattern"].IsString()) {
            capture_config.file_pattern = capture["file_pattern"].GetString();
        }
        if (capture.HasMember("max_file_size_mb") && capture["max_file_size_mb"].IsInt()) {
            capture_config.max_file_size_mb = capture["max_file_size_mb"].GetInt();
        }
    }


    if (doc.HasMember("storage") && doc["storage"].IsObject()) {
        const rapidjson::Value& storage = doc["storage"];
        if (storage.HasMember("base_dir") && storage["base_dir"].IsString()) {
            storage_config.base_dir = storage["base_dir"].GetString();
        }
        if (storage.HasMember("max_age_days") && storage["max_age_days"].IsInt()) {
            storage_config.max_age_days = storage["max_age_days"].GetInt();
        }
        if (storage.HasMember("max_size_gb") && storage["max_size_gb"].IsInt()) {
            storage_config.max_size_gb = storage["max_size_gb"].GetInt();
        }
        if (storage.HasMember("temp_pdef_dir") && storage["temp_pdef_dir"].IsString()) {
            storage_config.temp_pdef_dir = storage["temp_pdef_dir"].GetString();
        }
        if (storage.HasMember("temp_pdef_ttl_hours") && storage["temp_pdef_ttl_hours"].IsInt()) {
            storage_config.temp_pdef_ttl_hours = storage["temp_pdef_ttl_hours"].GetInt();
        }
    }


    if (doc.HasMember("cleanup") && doc["cleanup"].IsObject()) {
        const rapidjson::Value& cleanup = doc["cleanup"];
        if (cleanup.HasMember("compress_interval_sec") && cleanup["compress_interval_sec"].IsInt()) {
            cleanup_config.compress_interval_sec = cleanup["compress_interval_sec"].GetInt();
        }
        if (cleanup.HasMember("batch_compress_file_count") && cleanup["batch_compress_file_count"].IsInt()) {
            cleanup_config.batch_compress_file_count = cleanup["batch_compress_file_count"].GetInt();
        }
        if (cleanup.HasMember("batch_compress_size_mb") && cleanup["batch_compress_size_mb"].IsInt()) {
            cleanup_config.batch_compress_size_mb = cleanup["batch_compress_size_mb"].GetInt();
        }
        if (cleanup.HasMember("archive_dir") && cleanup["archive_dir"].IsString()) {
            cleanup_config.archive_dir = cleanup["archive_dir"].GetString();
        }
        if (cleanup.HasMember("archive_keep_days") && cleanup["archive_keep_days"].IsInt()) {
            cleanup_config.archive_keep_days = cleanup["archive_keep_days"].GetInt();
        }
        if (cleanup.HasMember("archive_max_total_size_mb") && cleanup["archive_max_total_size_mb"].IsInt()) {
            cleanup_config.archive_max_total_size_mb = cleanup["archive_max_total_size_mb"].GetInt();
        }
        if (cleanup.HasMember("archive_remove_source") && cleanup["archive_remove_source"].IsBool()) {
            cleanup_config.archive_remove_source = cleanup["archive_remove_source"].GetBool();
        }
    }


    if (doc.HasMember("limits") && doc["limits"].IsObject()) {
        const rapidjson::Value& limits = doc["limits"];
        if (limits.HasMember("max_concurrent_captures") && limits["max_concurrent_captures"].IsInt()) {
            limits_config.max_concurrent_captures = limits["max_concurrent_captures"].GetInt();
        }
    }

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
    return load_from_path("config/rxtracenetcap.json");
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

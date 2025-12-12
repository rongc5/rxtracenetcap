#include "rxstrategyconfig.h"
#include "rxprocdata.h"
#include "rxserverconfig.h"
#include "legacy_core.h"
#include "rapidjson/document.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <sys/stat.h>

SRxDefaults::SRxDefaults()
    : iface("any")
    , duration_sec(60)
    , category("diag")
    , file_pattern("{day}/{date}-{iface}-{proc}-{port}.pcap")
    , max_bytes(200 * 1024 * 1024L)
{
}

SRxLimits::SRxLimits()
    : max_concurrent_captures(3)
    , queue_capacity(1024)
    , per_key_cooldown_sec(120)
    , per_client_window_sec(10)
    , per_client_max_req(20)
    , high_priority_slots(2)
{
}

SRxStorage::SRxStorage()
    : base_dir("/var/log/rxtrace/captures")
    , max_age_days(7)
    , max_size_gb(100)
    , progress_interval_sec(10)
    , progress_packet_threshold(50000)
    , progress_bytes_threshold(100*1024*1024L)
    , compress_batch_interval_sec(300)
{
}

SRxThresholds::SRxThresholds()
    : cpu_pct_gt(85)
    , mem_pct_gt(90)
    , net_rx_kbps_gt(8000)
{
}

CRxSampleModule::CRxSampleModule()
    : cpu_pct_gt(0)
    , mem_pct_gt(0)
    , net_rx_kbps_gt(0)
    , capture_duration_sec(0)
    , cooldown_sec(0)
{
}

bool CRxSampleModule::has_thresholds() const
{
    return (cpu_pct_gt > 0) || (mem_pct_gt > 0) || (net_rx_kbps_gt > 0);
}

SRxLogging::SRxLogging()
    : log_path("logs")
    , prefix("rxtrace")
    , max_file_size_mb(100)
    , log_level(16)
{
}

CRxStrategyConfigManager::CRxStrategyConfigManager()
    : last_load_time_(0)
{
}

CRxStrategyConfigManager::~CRxStrategyConfigManager()
{
}

void CRxStrategyConfigManager::init(const std::string& config_path)
{
    config_path_ = config_path;
}

bool CRxStrategyConfigManager::load_internal(const std::string& config_path)
{

    std::ifstream ifs(config_path.c_str());
    if (!ifs.is_open()) {
        LOG_ERROR_MSG("Failed to open configuration file: %s", config_path.c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    ifs.close();


    rapidjson::Document doc;
    doc.Parse(content.c_str());

    if (doc.HasParseError()) {
        LOG_ERROR_MSG("Failed to parse JSON configuration file: %s (error at offset %zu)",
                      config_path.c_str(), doc.GetErrorOffset());
        return false;
    }

    if (!doc.IsObject()) {
        LOG_ERROR_MSG("Invalid JSON configuration file (not an object): %s", config_path.c_str());
        return false;
    }

    sample_modules_.clear();


    CRxProcData* pdata = CRxProcData::instance();
    if (pdata && pdata->server_config()) {
        const CRxServerConfig::CaptureConfig& cap = pdata->server_config()->capture();
        defaults_.iface = cap.default_interface;
        defaults_.duration_sec = cap.default_duration;
        defaults_.category = cap.default_category;
        defaults_.file_pattern = cap.file_pattern;
        defaults_.max_bytes = cap.max_file_size_mb * 1024L * 1024L;
    }


    if (doc.HasMember("sample") && doc["sample"].IsObject()) {
        const rapidjson::Value& sample = doc["sample"];

        if (sample.HasMember("worker_queue_size") && sample["worker_queue_size"].IsInt()) {
            limits_.queue_capacity = sample["worker_queue_size"].GetInt();
        }
        if (sample.HasMember("cpu_pct_gt") && sample["cpu_pct_gt"].IsInt()) {
            thresholds_.cpu_pct_gt = sample["cpu_pct_gt"].GetInt();
        }
        if (sample.HasMember("mem_pct_gt") && sample["mem_pct_gt"].IsInt()) {
            thresholds_.mem_pct_gt = sample["mem_pct_gt"].GetInt();
        }
        if (sample.HasMember("net_rx_kbps_gt") && sample["net_rx_kbps_gt"].IsInt()) {
            thresholds_.net_rx_kbps_gt = sample["net_rx_kbps_gt"].GetInt();
        }


        if (sample.HasMember("triggers") && sample["triggers"].IsArray()) {
            const rapidjson::Value& triggers = sample["triggers"];
            for (rapidjson::SizeType i = 0; i < triggers.Size(); i++) {
                const rapidjson::Value& trigger = triggers[i];
                if (!trigger.IsObject()) continue;

                CRxSampleModule module;

                if (trigger.HasMember("name") && trigger["name"].IsString()) {
                    module.name = trigger["name"].GetString();
                }
                if (trigger.HasMember("cpu_pct_gt") && trigger["cpu_pct_gt"].IsInt()) {
                    module.cpu_pct_gt = trigger["cpu_pct_gt"].GetInt();
                }
                if (trigger.HasMember("mem_pct_gt") && trigger["mem_pct_gt"].IsInt()) {
                    module.mem_pct_gt = trigger["mem_pct_gt"].GetInt();
                }
                if (trigger.HasMember("net_rx_kbps_gt") && trigger["net_rx_kbps_gt"].IsInt()) {
                    module.net_rx_kbps_gt = trigger["net_rx_kbps_gt"].GetInt();
                }
                if (trigger.HasMember("trigger_capture") && trigger["trigger_capture"].IsString()) {
                    module.capture_hint = trigger["trigger_capture"].GetString();
                }
                if (trigger.HasMember("capture_category") && trigger["capture_category"].IsString()) {
                    module.capture_category = trigger["capture_category"].GetString();
                }
                if (trigger.HasMember("capture_duration_sec") && trigger["capture_duration_sec"].IsInt()) {
                    module.capture_duration_sec = trigger["capture_duration_sec"].GetInt();
                } else {
                    module.capture_duration_sec = defaults_.duration_sec;
                }
                if (trigger.HasMember("cooldown_sec") && trigger["cooldown_sec"].IsInt()) {
                    module.cooldown_sec = trigger["cooldown_sec"].GetInt();
                }

                if (module.capture_duration_sec <= 0) {
                    module.capture_duration_sec = defaults_.duration_sec;
                }

                if (!module.has_thresholds()) {
                    LOG_WARNING_MSG("Skipping sample module '%s' because it defines no thresholds",
                                    module.name.c_str());
                    continue;
                }

                sample_modules_.push_back(module);
                LOG_NOTICE_MSG("Loaded sample module '%s' (cpu>%d mem>%d net>%d capture=%s duration=%d cooldown=%d)",
                               module.name.c_str(),
                               module.cpu_pct_gt,
                               module.mem_pct_gt,
                               module.net_rx_kbps_gt,
                               module.capture_hint.c_str(),
                               module.capture_duration_sec,
                               module.cooldown_sec);
            }
        }
    }


    if (pdata && pdata->server_config()) {
        const CRxServerConfig::StorageConfig& stor = pdata->server_config()->storage();
        storage_.base_dir = stor.base_dir;
        storage_.max_age_days = stor.max_age_days;
        storage_.max_size_gb = stor.max_size_gb;
    }


    if (doc.HasMember("server") && doc["server"].IsObject()) {
        const rapidjson::Value& server = doc["server"];

        (void)server;
    }


    protocol_pdefs_.clear();
    if (doc.HasMember("protocols") && doc["protocols"].IsObject()) {
        const rapidjson::Value& protocols = doc["protocols"];

        for (rapidjson::Value::ConstMemberIterator itr = protocols.MemberBegin();
             itr != protocols.MemberEnd(); ++itr) {
            if (itr->value.IsString()) {
                std::string protocol_name = itr->name.GetString();
                std::string pdef_path = itr->value.GetString();
                if (!pdef_path.empty()) {
                    protocol_pdefs_[protocol_name] = pdef_path;
                    LOG_NOTICE_MSG("Registered protocol mapping: %s -> %s",
                                   protocol_name.c_str(), pdef_path.c_str());
                }
            }
        }
    }

    last_load_time_ = query_mod_time(config_path);
    return true;
}


int CRxStrategyConfigManager::load()
{
    if (config_path_.empty()) {
        LOG_ERROR_MSG("Configuration path not initialized. Call init() first.");
        return -1;
    }

    CRxStrategyConfigManager temp;
    if (!temp.load_internal(config_path_)) {
        LOG_ERROR_MSG("Failed to load configuration from %s", config_path_.c_str());
        return -1;
    }

    defaults_ = temp.defaults_;
    limits_ = temp.limits_;
    storage_ = temp.storage_;
    thresholds_ = temp.thresholds_;
    sample_modules_ = temp.sample_modules_;
    logging_ = temp.logging_;
    protocol_pdefs_ = temp.protocol_pdefs_;
    last_load_time_ = temp.last_load_time_;

    LOG_NOTICE_MSG("Configuration loaded successfully from %s", config_path_.c_str());
    return 0;
}

int CRxStrategyConfigManager::reload()
{
    if (config_path_.empty()) {
        LOG_WARNING_MSG("Reload requested but no configuration path is set");
        return -1;
    }

    LOG_NOTICE_MSG("Attempting to reload configuration from %s", config_path_.c_str());

    CRxStrategyConfigManager temp_config_holder;
    if (!temp_config_holder.load_internal(config_path_)) {
        LOG_ERROR_MSG("Failed to load new configuration file, aborting reload.");
        return -1;
    }

    defaults_ = temp_config_holder.defaults_;
    limits_ = temp_config_holder.limits_;
    logging_ = temp_config_holder.logging_;
    thresholds_ = temp_config_holder.thresholds_;
    sample_modules_ = temp_config_holder.sample_modules_;
    storage_ = temp_config_holder.storage_;
    protocol_pdefs_ = temp_config_holder.protocol_pdefs_;
    last_load_time_ = temp_config_holder.last_load_time_;

    LOG_NOTICE_MSG("Configuration reloaded successfully");
    return 0;
}

bool CRxStrategyConfigManager::need_reload()
{
    if (config_path_.empty()) {
        return false;
    }
    long current = query_mod_time(config_path_);
    return (current > 0 && current != last_load_time_);
}

int CRxStrategyConfigManager::dump()
{
    return 0;
}

int CRxStrategyConfigManager::destroy()
{
    defaults_ = SRxDefaults();
    limits_ = SRxLimits();
    storage_ = SRxStorage();
    thresholds_ = SRxThresholds();
    sample_modules_.clear();
    logging_ = SRxLogging();
    protocol_pdefs_.clear();
    config_path_.clear();
    last_load_time_ = 0;
    return 0;
}

long CRxStrategyConfigManager::query_mod_time(const std::string& path) const
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return -1;
}

std::string CRxStrategyConfigManager::get_protocol_pdef_path(const std::string& protocol_name) const
{
    std::map<std::string, std::string>::const_iterator it = protocol_pdefs_.find(protocol_name);
    if (it != protocol_pdefs_.end()) {
        return it->second;
    }
    return std::string();
}

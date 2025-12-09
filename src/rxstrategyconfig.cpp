#include "rxstrategyconfig.h"
#include "legacy_core.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <sys/stat.h>

static std::string trim_copy(const std::string& value)
{
    const char* whitespace = " \t\r\n";
    const size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) return std::string();
    const size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

static std::string to_lower_copy(const std::string& value)
{
    std::string result = value;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    }
    return result;
}

static int parse_log_level_mask(const std::string& level_text, int fallback)
{
    if (level_text.empty()) {
        return fallback;
    }

    std::string normalized = to_lower_copy(level_text);
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] == '+' || normalized[i] == '|') {
            normalized[i] = ',';
        }
    }

    int mask = 0;
    std::stringstream ss(normalized);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim_copy(token);
        if (token.empty()) continue;
        if (token == "fatal") {
            mask |= LOG_FATAL;
        } else if (token == "warning" || token == "warn") {
            mask |= LOG_WARNING;
        } else if (token == "notice" || token == "info") {
            mask |= LOG_NOTICE;
        } else if (token == "trace") {
            mask |= LOG_TRACE;
        } else if (token == "debug") {
            mask |= LOG_DEBUG;
        }
    }

    return mask ? mask : fallback;
}

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
    , compress_enabled(true)
    , compress_cmd("gzip -9")
    , compress_remove_src(false)
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
    : sample_interval_sec_(1)
    , batch_compress_interval_sec_(300)
    , queue_timer_interval_ms_(50)
    , last_load_time_(0)
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
    CRxIniConfig ini;
    if (!ini.load(config_path)) {
        LOG_ERROR_MSG("Failed to load configuration file: %s", config_path.c_str());
        return false;
    }

    sample_modules_.clear();

    std::string iface = ini.value("capture", "default_interface", "");
    if (!iface.empty()) {
        defaults_.iface = iface;
    }
    defaults_.duration_sec = ini.intValue("capture", "default_duration", defaults_.duration_sec);
    std::string category = ini.value("capture", "default_category", "");
    if (!category.empty()) {
        defaults_.category = category;
    }
    std::string file_pattern = ini.value("capture", "file_pattern", "");
    if (!file_pattern.empty()) {
        defaults_.file_pattern = file_pattern;
    } else {
        std::string output_dir = ini.value("capture", "output_dir", "");
        std::string filename_prefix = ini.value("capture", "filename_prefix", "");
        if (!output_dir.empty() && !filename_prefix.empty()) {
            defaults_.file_pattern = output_dir + "/" + filename_prefix + "{ts}-{iface}.pcap";
        }
    }
    long max_file_size_mb = ini.intValue("capture", "max_file_size_mb", (int)(defaults_.max_bytes / (1024 * 1024)));
    if (max_file_size_mb < 0) {
        max_file_size_mb = (long)(defaults_.max_bytes / (1024 * 1024));
    }
    defaults_.max_bytes = max_file_size_mb * 1024L * 1024L;

    limits_.max_concurrent_captures = ini.intValue("security", "max_concurrent_captures", limits_.max_concurrent_captures);
    limits_.queue_capacity = ini.intValue("sample", "worker_queue_size", limits_.queue_capacity);

    std::string storage_dir = ini.value("storage", "base_dir", "");
    if (!storage_dir.empty()) {
        storage_.base_dir = storage_dir;
    }
    storage_.compress_enabled = ini.boolValue("storage", "compress_enabled", storage_.compress_enabled);
    std::string compress_cmd = ini.value("storage", "compress_cmd", "");
    if (!compress_cmd.empty()) {
        storage_.compress_cmd = compress_cmd;
    }
    storage_.compress_remove_src = ini.boolValue("storage", "compress_remove_src", storage_.compress_remove_src);
    storage_.max_age_days = ini.intValue("storage", "max_age_days", storage_.max_age_days);
    storage_.max_size_gb = ini.intValue("storage", "max_size_gb", storage_.max_size_gb);

    storage_.progress_interval_sec = ini.intValue("storage", "progress_interval_sec", storage_.progress_interval_sec);
    if (storage_.progress_interval_sec < 1) storage_.progress_interval_sec = 10;

    storage_.progress_packet_threshold = ini.intValue("storage", "progress_packet_threshold", storage_.progress_packet_threshold);
    if (storage_.progress_packet_threshold < 0) storage_.progress_packet_threshold = 50000;

    long progress_bytes_mb = ini.intValue("storage", "progress_bytes_threshold_mb", (int)(storage_.progress_bytes_threshold / (1024*1024)));
    if (progress_bytes_mb > 0) {
        storage_.progress_bytes_threshold = progress_bytes_mb * 1024L * 1024L;
    }

    storage_.compress_batch_interval_sec = ini.intValue("storage", "compress_batch_interval_sec", storage_.compress_batch_interval_sec);
    if (storage_.compress_batch_interval_sec < 1) storage_.compress_batch_interval_sec = 60;

    LOG_DEBUG_MSG("Progress reporting: interval=%d sec, packet_threshold=%d, bytes_threshold=%ldMB",
                  storage_.progress_interval_sec, storage_.progress_packet_threshold,
                  storage_.progress_bytes_threshold / (1024*1024));

    std::string log_path = ini.value("logging", "log_path", "");
    if (!log_path.empty()) {
        logging_.log_path = log_path;
    }
    std::string log_file = ini.value("logging", "log_file", "");
    if (!log_file.empty()) {
        std::string file = log_file;
        size_t slash = file.find_last_of('/');
        if (slash != std::string::npos) {
            if (log_path.empty()) {
                logging_.log_path = file.substr(0, slash);
            }
            std::string base = file.substr(slash + 1);
            size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) {
                base = base.substr(0, dot);
            }
            if (!base.empty()) {
                logging_.prefix = base;
            }
        } else if (!file.empty()) {
            if (log_path.empty()) {
                logging_.log_path = ".";
            }
            size_t dot = file.find_last_of('.');
            std::string base = (dot != std::string::npos) ? file.substr(0, dot) : file;
            if (!base.empty()) {
                logging_.prefix = base;
            }
        }
    }
    std::string prefix_override = ini.value("logging", "prefix", "");
    if (!prefix_override.empty()) {
        logging_.prefix = prefix_override;
    }
    std::string level_text = ini.value("logging", "log_level", "");
    if (!level_text.empty()) {
        logging_.log_level = parse_log_level_mask(level_text, logging_.log_level);
    }
    logging_.max_file_size_mb = ini.intValue("logging", "max_file_size_mb", logging_.max_file_size_mb);

    thresholds_.cpu_pct_gt = ini.intValue("sample", "cpu_pct_gt", thresholds_.cpu_pct_gt);
    thresholds_.mem_pct_gt = ini.intValue("sample", "mem_pct_gt", thresholds_.mem_pct_gt);
    thresholds_.net_rx_kbps_gt = ini.intValue("sample", "net_rx_kbps_gt", thresholds_.net_rx_kbps_gt);

    const std::string module_prefix = "sample.";
    std::vector<std::string> sections = ini.sections();
    for (std::vector<std::string>::const_iterator sit = sections.begin();
         sit != sections.end(); ++sit) {
        const std::string& section = *sit;
        if (section.size() <= module_prefix.size()) {
            continue;
        }
        if (section.compare(0, module_prefix.size(), module_prefix) != 0) {
            continue;
        }

        std::string module_name = trim_copy(section.substr(module_prefix.size()));
        if (module_name.empty()) {
            LOG_WARNING_MSG("Ignoring sample module with empty name (section=%s)", section.c_str());
            continue;
        }

        CRxSampleModule module;
        module.name = module_name;
        module.cpu_pct_gt = ini.intValue(section, "cpu_pct_gt", 0);
        module.mem_pct_gt = ini.intValue(section, "mem_pct_gt", 0);
        module.net_rx_kbps_gt = ini.intValue(section, "net_rx_kbps_gt", 0);
        module.capture_hint = ini.value(section, "trigger_capture", "");
        module.capture_category = ini.value(section, "capture_category", "");
        module.capture_duration_sec = ini.intValue(section, "capture_duration_sec", defaults_.duration_sec);
        module.cooldown_sec = ini.intValue(section, "cooldown_sec", 0);

        if (module.capture_duration_sec <= 0) {
            module.capture_duration_sec = defaults_.duration_sec;
        }

        if (!module.has_thresholds()) {
            LOG_WARNING_MSG("Skipping sample module '%s' because it defines no thresholds", module.name.c_str());
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

    sample_interval_sec_ = ini.intValue("server", "sample_interval_sec", sample_interval_sec_);
    batch_compress_interval_sec_ = ini.intValue("server", "batch_compress_interval_sec", batch_compress_interval_sec_);
    queue_timer_interval_ms_ = ini.intValue("server", "queue_timer_interval_ms", queue_timer_interval_ms_);

    /* Load protocol PDEF mappings from [protocols] section */
    protocol_pdefs_.clear();
    std::vector<std::string> protocol_keys = ini.keys("protocols");
    for (std::vector<std::string>::const_iterator kit = protocol_keys.begin();
         kit != protocol_keys.end(); ++kit) {
        const std::string& protocol_name = *kit;
        std::string pdef_path = ini.value("protocols", protocol_name, "");
        if (!pdef_path.empty()) {
            protocol_pdefs_[protocol_name] = pdef_path;
            LOG_NOTICE_MSG("Registered protocol mapping: %s -> %s",
                           protocol_name.c_str(), pdef_path.c_str());
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
    sample_interval_sec_ = temp.sample_interval_sec_;
    batch_compress_interval_sec_ = temp.batch_compress_interval_sec_;
    queue_timer_interval_ms_ = temp.queue_timer_interval_ms_;
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
    sample_interval_sec_ = temp_config_holder.sample_interval_sec_;
    batch_compress_interval_sec_ = temp_config_holder.batch_compress_interval_sec_;
    queue_timer_interval_ms_ = temp_config_holder.queue_timer_interval_ms_;
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
    sample_interval_sec_ = 1;
    batch_compress_interval_sec_ = 300;
    queue_timer_interval_ms_ = 50;
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
    return std::string();  /* Empty string if not found */
}

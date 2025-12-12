#ifndef RXNET_STRATEGY_CONFIG_MANAGER_H
#define RXNET_STRATEGY_CONFIG_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <time.h>
#include "legacy_core.h"

struct SRxDefaults {
    std::string iface;
    int duration_sec;
    std::string category;
    std::string file_pattern;
    long max_bytes;

    SRxDefaults();
};

struct SRxLimits {
    int max_concurrent_captures;
    int queue_capacity;
    int per_key_cooldown_sec;
    int per_client_window_sec;
    int per_client_max_req;
    int high_priority_slots;

    SRxLimits();
};

struct SRxStorage {
    std::string base_dir;
    int max_age_days;
    long max_size_gb;

    int progress_interval_sec;
    int progress_packet_threshold;
    long progress_bytes_threshold;
    int compress_batch_interval_sec;

    SRxStorage();
};

struct SRxThresholds {
    int cpu_pct_gt;
    int mem_pct_gt;
    int net_rx_kbps_gt;

    SRxThresholds();
};

struct CRxSampleModule {
    std::string name;
    int cpu_pct_gt;
    int mem_pct_gt;
    int net_rx_kbps_gt;
    std::string capture_hint;
    int capture_duration_sec;
    int cooldown_sec;
    std::string capture_category;

    CRxSampleModule();
    bool has_thresholds() const;
};

struct SRxLogging {
    std::string log_path;
    std::string prefix;
    unsigned int max_file_size_mb;
    int log_level;

    SRxLogging();
};

class CRxStrategyConfigManager : public reload_inf {
public:
    CRxStrategyConfigManager();
    ~CRxStrategyConfigManager();

    void init(const std::string& config_path);

    virtual int load();
    virtual int reload();
    virtual bool need_reload();
    virtual int dump();
    virtual int destroy();

    const SRxDefaults& defaults() const { return defaults_; }
    const SRxLimits& limits() const { return limits_; }
    const SRxStorage& storage() const { return storage_; }
    const SRxThresholds& thresholds() const { return thresholds_; }
    const std::vector<CRxSampleModule>& sample_modules() const { return sample_modules_; }
    const SRxLogging& logging() const { return logging_; }

    std::string get_default_iface() const { return defaults_.iface; }
    int get_default_duration() const { return defaults_.duration_sec; }
    std::string get_default_category() const { return defaults_.category; }

    int get_progress_interval_sec() const { return storage_.progress_interval_sec; }
    int get_progress_packet_threshold() const { return storage_.progress_packet_threshold; }
    long get_progress_bytes_threshold() const { return storage_.progress_bytes_threshold; }


    std::string get_protocol_pdef_path(const std::string& protocol_name) const;

private:

    CRxStrategyConfigManager(const CRxStrategyConfigManager&);
    CRxStrategyConfigManager& operator=(const CRxStrategyConfigManager&);

    SRxDefaults defaults_;
    SRxLimits limits_;
    SRxStorage storage_;
    SRxThresholds thresholds_;
    std::vector<CRxSampleModule> sample_modules_;
    SRxLogging logging_;
    std::map<std::string, std::string> protocol_pdefs_;

    std::string config_path_;
    time_t last_load_time_;

    bool load_internal(const std::string& config_path);
    long query_mod_time(const std::string& path) const;
};

#endif

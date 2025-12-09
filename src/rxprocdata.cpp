#include "legacy_core.h"
#include "rxprocdata.h"
#include <new>
#include "rxcapturemanagerthread.h"
#include "rxsamplethread.h"
#include "rxcleanupthread.h"
#include "rxreloadthread.h"
#include "rxhttpresdataprocess.h"
#include "rxhttpthread.h"
#include "rxcapturemessages.h"
#include "rxurlhandlers.h"
#include <time.h>

namespace {

inline uint32_t fnv1a_append(uint32_t hash, const unsigned char* data, size_t len)
{
    const uint32_t kPrime = 16777619u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= kPrime;
    }
    return hash;
}

inline uint32_t fnv1a_mix_uint32(uint32_t hash, uint32_t value)
{
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>(value & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    return fnv1a_append(hash, bytes, sizeof(bytes));
}

inline uint32_t fnv1a_mix_uint64(uint32_t hash, uint64_t value)
{
    unsigned char bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
    }
    return fnv1a_append(hash, bytes, sizeof(bytes));
}

inline uint32_t fnv1a_mix_string(uint32_t hash, const std::string& value)
{
    if (!value.empty()) {
        hash = fnv1a_append(hash,
                             reinterpret_cast<const unsigned char*>(value.data()),
                             value.size());
    }
    hash = fnv1a_append(hash, reinterpret_cast<const unsigned char*>("\0"), 1);
    return hash;
}

uint32_t compute_config_hash(const CaptureConfigSnapshot& cfg)
{
    uint32_t hash = 2166136261u;
    hash = fnv1a_mix_string(hash, cfg.output_dir);
    hash = fnv1a_mix_string(hash, cfg.filename_template);
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.file_rotate_size_mb));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.file_rotate_count));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.max_duration_sec));
    hash = fnv1a_mix_uint64(hash, static_cast<uint64_t>(cfg.max_bytes));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.max_packets));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.snaplen));
    hash = fnv1a_mix_uint32(hash, cfg.compress_enabled ? 1u : 0u);
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.compress_threshold_mb));
    hash = fnv1a_mix_string(hash, cfg.compress_format);
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.compress_level));
    hash = fnv1a_mix_uint32(hash, cfg.compress_remove_src ? 1u : 0u);
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.retain_days));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.clean_batch_size));
    hash = fnv1a_mix_uint64(hash, static_cast<uint64_t>(cfg.disk_threshold_gb));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.progress_interval_sec));
    hash = fnv1a_mix_uint32(hash, static_cast<uint32_t>(cfg.progress_packet_threshold));
    hash = fnv1a_mix_uint64(hash, static_cast<uint64_t>(cfg.progress_bytes_threshold));
    return hash;
}

}

CRxProcData::CRxProcData()
    : _strategy_dict(NULL)
    , _conf(NULL)
    , _capture_manager_thread(NULL)
    , _sample_thread(NULL)
    , _cleanup_thread(NULL)
    , _reload_thread(NULL)
    , _threads_initialized(false)
    , _next_sample_alert_id(1)
{
}

CRxProcData::~CRxProcData()
{
    LOG_NOTICE("proc_data destructing, cleaning up resources...");

    for (std::map<std::string, std::vector<base_net_thread*> >::iterator it = _name_thread_map.begin();
         it != _name_thread_map.end(); ++it)
    {
        for (std::vector<base_net_thread*>::iterator vec_it = it->second.begin();
             vec_it != it->second.end(); ++vec_it)
        {
            if (*vec_it)
            {
                delete *vec_it;
            }
        }
    }
    _name_thread_map.clear();

    _capture_manager_thread = NULL;
    _sample_thread = NULL;
    _cleanup_thread = NULL;
    _reload_thread = NULL;

    LOG_NOTICE("proc_data resources cleaned up");
}

int CRxProcData::init(CRxServerConfig *conf)
{
    _conf = conf;

    get_proc_name(proc_name, sizeof(proc_name));

    CRxStrategyConfigManager *conf1 = new (std::nothrow)CRxStrategyConfigManager();
    if (conf1) {
        conf1->init(_conf ? _conf->strategy_path() : std::string());
    }
    if (!conf1) {
        LOG_WARNING("allocate CRxStrategyConfigManager fail");
    }

    CRxStrategyConfigManager *conf2 = new (std::nothrow)CRxStrategyConfigManager();
    if (conf2) {
        conf2->init(_conf ? _conf->strategy_path() : std::string());
    }
    if (!conf2) {
        LOG_WARNING("allocate CRxStrategyConfigManager fail");
    }

    _strategy_dict = new (std::nothrow)reload_mgr<CRxStrategyConfigManager>(conf1, conf2);

    reg_handler();

    return 0;
}

void CRxProcData::add_name_thread(const std::string& name, base_net_thread* thread)
{
    std::map<std::string, std::vector<base_net_thread*> >::iterator it = _name_thread_map.find(name);
    if (it != _name_thread_map.end())
    {
        it->second.push_back(thread);
    }
    else
    {
        std::vector<base_net_thread*> vec;
        vec.push_back(thread);
        _name_thread_map[name] = vec;
    }
}

void CRxProcData::add_name_thread(const std::string& name, std::vector<base_net_thread*>& threads)
{
    _name_thread_map[name] = threads;
}

std::vector<base_net_thread*>* CRxProcData::get_thread(const std::string& name)
{
    std::map<std::string, std::vector<base_net_thread*> >::iterator it = _name_thread_map.find(name);
    if (it != _name_thread_map.end())
    {
        return &(it->second);
    }
    return NULL;
}

void CRxProcData::reg_handler()
{
    url_handler_map_.clear();

    shared_ptr<CRxUrlHandler> handler;

    handler.reset(new CRxUrlHandlerStaticJson(200, "OK", "{\"status\":\"ok\"}"));
    url_handler_map_.insert(std::make_pair("/health", handler));

    handler.reset(new CRxUrlHandlerStaticJson(200, "OK", "{\"message\":\"rxtracenetcap http service\"}"));
    url_handler_map_.insert(std::make_pair("/", handler));
    url_handler_map_.insert(std::make_pair("", handler));

    shared_ptr<CRxUrlHandler> capture_handler(new CRxUrlHandlerCaptureApi());
    url_handler_map_.insert(std::make_pair("/api/capture/start", capture_handler));
    url_handler_map_.insert(std::make_pair("/api/capture/stop", capture_handler));
    url_handler_map_.insert(std::make_pair("/api/capture/status", capture_handler));

    shared_ptr<CRxUrlHandler> pdef_upload_handler(new CRxUrlHandlerPdefUpload());
    url_handler_map_.insert(std::make_pair("/api/pdef/upload", pdef_upload_handler));

    shared_ptr<CRxUrlHandler> pdef_mgmt_handler(new CRxUrlHandlerPdefManagement());
    url_handler_map_.insert(std::make_pair("/api/pdef/list", pdef_mgmt_handler));
    url_handler_map_.insert(std::make_pair("/api/pdef/get", pdef_mgmt_handler));

    shared_ptr<CRxUrlHandler> default_handler(
        new CRxUrlHandlerStaticJson(404, "Not Found", "{\"error\":\"no handler registered\"}"));
    url_handler_map_.insert(std::make_pair("/default", default_handler));
}

shared_ptr<CRxUrlHandler> CRxProcData::get_url_handler(const std::string& key)
{
    std::string normalized = key;
    size_t q = normalized.find('?');
    if (q != std::string::npos) {
        normalized.erase(q);
    }
    std::map<std::string, shared_ptr<CRxUrlHandler> >::iterator it = url_handler_map_.find(normalized);
    if (it != url_handler_map_.end())
    {
        return it->second;
    }

    it = url_handler_map_.find("/default");
    if (it != url_handler_map_.end())
    {
        return it->second;
    }

    return shared_ptr<CRxUrlHandler>();
}

CRxProcData* CRxProcData::_singleton;

CRxProcData * CRxProcData::instance()
{
    if(!_singleton)
    {
        _singleton = new(std::nothrow)CRxProcData();
        if(_singleton == NULL)
        {
            LOG_WARNING("new Proc_data fail");
            exit(1);
        }
    }

    return _singleton;
}

int CRxProcData::load()
{
    if (_strategy_dict)
    {
        _strategy_dict->load();
    }

    if (!_threads_initialized)
    {
        init_threads();
    }

    return 0;
}

int CRxProcData::init_threads()
{
    if (_threads_initialized)
    {
        LOG_WARNING("Threads already initialized");
        return 0;
    }

    LOG_NOTICE("Initializing all threads...");

    _capture_manager_thread = new (std::nothrow) CRxCaptureManagerThread();
    if (!_capture_manager_thread)
    {
        LOG_ERROR("Failed to allocate CRxCaptureManagerThread");
        return -1;
    }

    _sample_thread = new (std::nothrow) CRxSampleThread();
    if (!_sample_thread)
    {
        LOG_ERROR("Failed to allocate CRxSampleThread");
        return -1;
    }

    _cleanup_thread = new (std::nothrow) CRxCleanupThread();
    if (!_cleanup_thread)
    {
        LOG_ERROR("Failed to allocate CRxCleanupThread");
        return -1;
    }
    if (_conf) {
        _cleanup_thread->configure(_conf->cleanup());
    }

    _reload_thread = new (std::nothrow) CRxReloadThread();
    if (!_reload_thread)
    {
        LOG_ERROR("Failed to allocate CRxReloadThread");
        return -1;
    }

    LOG_NOTICE("Starting CRxCaptureManagerThread...");
    if (!_capture_manager_thread->start())
    {
        LOG_ERROR("Failed to start CRxCaptureManagerThread");
        return -1;
    }

    LOG_NOTICE("Starting CRxSampleThread...");
    if (!_sample_thread->start())
    {
        LOG_ERROR("Failed to start CRxSampleThread");
        return -1;
    }

    LOG_NOTICE("Starting CRxCleanupThread...");
    if (!_cleanup_thread->start())
    {
        LOG_ERROR("Failed to start CRxCleanupThread");
        return -1;
    }

    LOG_NOTICE("Starting CRxReloadThread...");
    if (!_reload_thread->start())
    {
        LOG_ERROR("Failed to start CRxReloadThread");
        return -1;
    }

    add_name_thread("capture_manager", _capture_manager_thread);
    add_name_thread("sample", _sample_thread);
    add_name_thread("cleanup", _cleanup_thread);
    add_name_thread("reload", _reload_thread);

    _threads_initialized = true;

    LOG_NOTICE("All threads initialized successfully");

    return 0;
}

int CRxProcData::reload()
{
    int flag = 0;
    if (_strategy_dict && _strategy_dict->need_reload())
    {
        _strategy_dict->reload();
        flag = 1;
    }

    return flag;
}

bool CRxProcData::need_reload()
{
    return true;
}

int CRxProcData::dump()
{
    if (_strategy_dict)
    {
        _strategy_dict->dump();
    }

    return 0;
}

int CRxProcData::destroy()
{
    if (_strategy_dict)
    {
        _strategy_dict->destroy();
    }

    return 0;
}

int CRxProcData::destroy_idle()
{
    if (_conf)
    {
        _strategy_dict->idle()->destroy();
    }

    return 0;
}

CaptureConfigSnapshot CRxProcData::get_capture_config_snapshot() const
{
    CaptureConfigSnapshot snapshot;

    const CRxStrategyConfigManager* cfg_manager = NULL;
    if (_strategy_dict) {
        cfg_manager = _strategy_dict->current();
    }

    if (cfg_manager) {
        const SRxDefaults& defaults = cfg_manager->defaults();
        const SRxStorage& storage = cfg_manager->storage();

        snapshot.output_dir = storage.base_dir;
        snapshot.filename_template = defaults.file_pattern;
        if (defaults.max_bytes > 0) {
            snapshot.file_rotate_size_mb = static_cast<int>(defaults.max_bytes / (1024 * 1024));
            if (snapshot.file_rotate_size_mb <= 0) {
                snapshot.file_rotate_size_mb = 1;
            }
        } else {
            snapshot.file_rotate_size_mb = 0;
        }
        snapshot.file_rotate_count = 0;

        snapshot.max_duration_sec = defaults.duration_sec;
        snapshot.max_bytes = defaults.max_bytes;
        snapshot.max_packets = 0;
        snapshot.snaplen = 65535;

        snapshot.compress_enabled = storage.compress_enabled;
        if (storage.progress_bytes_threshold > 0) {
            snapshot.compress_threshold_mb = static_cast<int>(storage.progress_bytes_threshold / (1024 * 1024));
            if (snapshot.compress_threshold_mb <= 0) {
                snapshot.compress_threshold_mb = 1;
            }
        } else {
            snapshot.compress_threshold_mb = 0;
        }
        snapshot.compress_format = storage.compress_cmd;
        snapshot.compress_level = 0;
        snapshot.compress_remove_src = storage.compress_remove_src;

        snapshot.retain_days = storage.max_age_days;
        snapshot.clean_batch_size = storage.compress_batch_interval_sec;
        snapshot.disk_threshold_gb = storage.max_size_gb;

        snapshot.progress_interval_sec = storage.progress_interval_sec;
        snapshot.progress_packet_threshold = storage.progress_packet_threshold;
        snapshot.progress_bytes_threshold = storage.progress_bytes_threshold;
    }

    snapshot.config_hash = compute_config_hash(snapshot);
    snapshot.config_timestamp = static_cast<int64_t>(time(NULL));
    return snapshot;
}

uint64_t CRxProcData::record_sample_alert(const SRxSampleAlertRecord& alert)
{
    SRxSampleAlertRecord stored = alert;
    stored.alert_id = _next_sample_alert_id++;
    if (_next_sample_alert_id == 0) {
        _next_sample_alert_id = 1;
    }
    if (stored.timestamp == 0) {
        stored.timestamp = static_cast<int64_t>(time(NULL));
    }
    if (_sample_alerts.size() >= 128) {
        _sample_alerts.pop_front();
    }
    _sample_alerts.push_back(stored);
    LOG_NOTICE("Recorded sample alert id=%llu module=%s cpu=%.2f%% mem=%.2f%% rx=%.2fKB/s tx=%.2fKB/s",
               static_cast<unsigned long long>(stored.alert_id),
               stored.module_name.c_str(),
               stored.cpu_percent,
               stored.memory_percent,
               stored.network_rx_kbps,
               stored.network_tx_kbps);
    return stored.alert_id;
}

void CRxProcData::get_sample_alerts(std::vector<SRxSampleAlertRecord>& out, size_t max_count) const
{
    out.clear();
    if (max_count == 0 || _sample_alerts.empty()) {
        return;
    }
    size_t count = _sample_alerts.size();
    if (count > max_count) {
        count = max_count;
    }
    out.reserve(count);
    std::deque<SRxSampleAlertRecord>::const_reverse_iterator it = _sample_alerts.rbegin();
    for (size_t i = 0; i < count && it != _sample_alerts.rend(); ++i, ++it) {
        out.push_back(*it);
    }
}

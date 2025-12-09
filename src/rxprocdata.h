#ifndef __RX_PROC_DATA_H__
#define __RX_PROC_DATA_H__

#include "legacy_core.h"
#include "rxstrategyconfig.h"
#include "rxserverconfig.h"
#include "rxurlhandler.h"
#include "rxcapturetasktypes.h"
#include "rxcapturemessages.h"
#include "rxsafetaskmgr.h"
#include <vector>
#include <deque>
#include <stdint.h>
#include <map>
#include <string>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

class base_net_thread;
class CRxCaptureManagerThread;
class CRxSampleThread;
class CRxCleanupThread;
class CRxReloadThread;

struct SRxSampleAlertRecord {
    uint64_t alert_id;
    time_t timestamp;
    double cpu_percent;
    double memory_percent;
    double network_rx_kbps;
    double network_tx_kbps;
    bool cpu_hit;
    bool mem_hit;
    bool net_hit;
    std::string module_name;
    std::string capture_hint;
    std::string capture_category;
    int capture_duration_sec;
    int cooldown_sec;
    int cpu_threshold;
    int mem_threshold;
    int net_threshold;

    SRxSampleAlertRecord()
        : alert_id(0)
        , timestamp(0)
        , cpu_percent(0.0)
        , memory_percent(0.0)
        , network_rx_kbps(0.0)
        , network_tx_kbps(0.0)
        , cpu_hit(false)
        , mem_hit(false)
        , net_hit(false)
        , capture_duration_sec(0)
        , cooldown_sec(0)
        , cpu_threshold(0)
        , mem_threshold(0)
        , net_threshold(0)
    {
    }
};

class CRxProcData:public reload_inf
{
    public:
        CRxProcData();
        virtual ~CRxProcData();

        static CRxProcData * instance();

        int init(CRxServerConfig *conf);
        virtual int load();
        virtual int reload();
        virtual bool need_reload();
        virtual int dump();
        virtual int destroy();
        virtual int destroy_idle();

        int init_threads();

        CRxCaptureManagerThread* get_capture_manager_thread() { return _capture_manager_thread; }
        CRxSampleThread* get_sample_thread() { return _sample_thread; }
        CRxCleanupThread* get_cleanup_thread() { return _cleanup_thread; }
        CRxReloadThread* get_reload_thread() { return _reload_thread; }

        void add_name_thread(const std::string& name, base_net_thread* thread);
        void add_name_thread(const std::string& name, std::vector<base_net_thread*>& threads);
        std::vector<base_net_thread*>* get_thread(const std::string& name);

        void reg_handler();
        shared_ptr<CRxUrlHandler> get_url_handler(const std::string& key);

        CRxSafeTaskMgr& capture_task_mgr() { return _capture_task_mgr; }
        CRxServerConfig* server_config() const { return _conf; }
        CaptureConfigSnapshot get_capture_config_snapshot() const;
        CRxStrategyConfigManager* current_strategy_config() const {
            return _strategy_dict ? _strategy_dict->current() : NULL;
        }
        uint64_t record_sample_alert(const SRxSampleAlertRecord& alert);
        void get_sample_alerts(std::vector<SRxSampleAlertRecord>& out, size_t max_count = 32) const;

    public:
        reload_mgr<CRxStrategyConfigManager> * _strategy_dict;

    public:
        char proc_name[SIZE_LEN_256];

    private:
        CRxServerConfig * _conf;
        std::map<std::string, std::vector<base_net_thread*> > _name_thread_map;
        std::map<std::string, shared_ptr<CRxUrlHandler> > url_handler_map_;

        CRxCaptureManagerThread* _capture_manager_thread;
        CRxSampleThread* _sample_thread;
        CRxCleanupThread* _cleanup_thread;
        CRxReloadThread* _reload_thread;
        bool _threads_initialized;

        CRxSafeTaskMgr _capture_task_mgr;
        uint64_t _next_sample_alert_id;
        std::deque<SRxSampleAlertRecord> _sample_alerts;

    private:
        static CRxProcData* _singleton;
};

#endif

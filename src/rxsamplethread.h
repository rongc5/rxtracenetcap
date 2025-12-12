#ifndef RX_SAMPLE_THREAD_H
#define RX_SAMPLE_THREAD_H

#include "legacy_core.h"
#include "rxstrategyconfig.h"
#include "rxmsgtypes.h"

#include <string>
#include <vector>
#include <stdint.h>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;


static const int SAMPLE_INTERVAL_SEC = 15;

struct SRxSystemStats {
    double cpu_percent;
    double memory_percent;
    double network_rx_kbps;
    double network_tx_kbps;
    time_t timestamp;

    SRxSystemStats() : cpu_percent(0), memory_percent(0),
                       network_rx_kbps(0), network_tx_kbps(0), timestamp(0) {}
};

struct SRxSampleMsg : public normal_msg {
    SRxSystemStats stats;
    bool cpu_hit;
    bool mem_hit;
    bool net_hit;
    uint64_t alert_id;
    std::string module_name;
    std::string capture_hint;
    std::string capture_category;
    int capture_duration_sec;
    int cooldown_sec;
    int cpu_threshold;
    int mem_threshold;
    int net_threshold;
    SRxSampleMsg()
        : cpu_hit(false)
        , mem_hit(false)
        , net_hit(false)
        , alert_id(0)
        , capture_duration_sec(0)
        , cooldown_sec(0)
        , cpu_threshold(0)
        , mem_threshold(0)
        , net_threshold(0)
    {
        _msg_op = RX_MSG_SAMPLE_TRIGGER;
    }
};

class CRxSampleThread : public base_net_thread {
public:
    CRxSampleThread();
    ~CRxSampleThread();
    bool start();

    void set_name(const std::string& n) { name_ = n; }
    const std::string& name() const { return name_; }

    void set_type(int t) { type_ = t; }
    int type() const { return type_; }

protected:
    virtual void handle_timeout(shared_ptr<timer_msg>& t_msg);

private:
    enum { SAMPLE_TIMER_TYPE = 1 };
    void schedule_timer();
    void sample_and_check();
    void load_config();
    void sample_system_stats(SRxSystemStats& stats);
    void emit_alert(const SRxSystemStats& stats,
                    const CRxSampleModule* module,
                    bool cpu_hit,
                    bool mem_hit,
                    bool net_hit);

private:
    SRxThresholds thr_;
    std::vector<CRxSampleModule> modules_;
    int type_;
    std::string name_;
};

#endif

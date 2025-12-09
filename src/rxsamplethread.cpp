#include "rxsamplethread.h"
#include "rxstrategyconfig.h"
#include "rxprocdata.h"
#include "rxcapturemanagerthread.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {
    const int RX_THREAD_SAMPLE_TYPE = 4;
}

CRxSampleThread::CRxSampleThread()
    : base_net_thread(1), interval_(1), type_(RX_THREAD_SAMPLE_TYPE)
{
}

CRxSampleThread::~CRxSampleThread()
{
}

bool CRxSampleThread::start()
{
    load_config();

    if (!base_net_thread::start()) {
        return false;
    }
    if (name_.empty()) {
        name_ = "sample";
    }
    schedule_timer();
    return true;
}

void CRxSampleThread::handle_timeout(shared_ptr<timer_msg>& t_msg)
{
    if (!t_msg) {
        return;
    }
    if (t_msg->_timer_type != SAMPLE_TIMER_TYPE) {
        base_net_thread::handle_timeout(t_msg);
        return;
    }
    sample_and_check();
    if (get_run_flag()) {
        schedule_timer();
    }
}

void CRxSampleThread::schedule_timer()
{
    shared_ptr<timer_msg> t_msg(new timer_msg);
    t_msg->_obj_id = OBJ_ID_THREAD;
    t_msg->_timer_type = SAMPLE_TIMER_TYPE;
    t_msg->_time_length = static_cast<uint32_t>(interval_ * 1000);
    add_timer(t_msg);
}

void CRxSampleThread::load_config()
{
    CRxStrategyConfigManager* cfg = CRxProcData::instance()->current_strategy_config();
    if (cfg) {
        interval_ = cfg->sample_interval_sec();
        thr_ = cfg->thresholds();
        modules_ = cfg->sample_modules();
    } else {
        modules_.clear();
    }
}

void CRxSampleThread::sample_and_check()
{
    SRxSystemStats s;
    sample_system_stats(s);

    if (!modules_.empty()) {
        for (std::vector<CRxSampleModule>::const_iterator it = modules_.begin();
             it != modules_.end(); ++it) {
            const CRxSampleModule& module = *it;
            bool cpu_hit = (module.cpu_pct_gt > 0 && s.cpu_percent > module.cpu_pct_gt);
            bool mem_hit = (module.mem_pct_gt > 0 && s.memory_percent > module.mem_pct_gt);
            bool net_hit = (module.net_rx_kbps_gt > 0 && s.network_rx_kbps > module.net_rx_kbps_gt);
            if (!(cpu_hit || mem_hit || net_hit)) {
                continue;
            }
            emit_alert(s, &module, cpu_hit, mem_hit, net_hit);
        }
        return;
    }

    bool cpu_hit = (thr_.cpu_pct_gt > 0 && s.cpu_percent > thr_.cpu_pct_gt);
    bool mem_hit = (thr_.mem_pct_gt > 0 && s.memory_percent > thr_.mem_pct_gt);
    bool net_hit = (thr_.net_rx_kbps_gt > 0 && s.network_rx_kbps > thr_.net_rx_kbps_gt);
    if (cpu_hit || mem_hit || net_hit) {
        emit_alert(s, NULL, cpu_hit, mem_hit, net_hit);
    }
}

void CRxSampleThread::sample_system_stats(SRxSystemStats& stats)
{
    stats.timestamp = time(NULL);

    FILE* f = fopen("/proc/stat", "r");
    if (f) {
        unsigned long user, nice, system, idle;
        if (fscanf(f, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle) == 4) {
            unsigned long total = user + nice + system + idle;
            if (total > 0) {
                stats.cpu_percent = ((double)(total - idle) / total) * 100.0;
            }
        }
        fclose(f);
    }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        unsigned long mem_total = 0, mem_available = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", &mem_total);
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line + 13, "%lu", &mem_available);
                break;
            }
        }
        if (mem_total > 0) {
            stats.memory_percent = ((double)(mem_total - mem_available) / mem_total) * 100.0;
        }
        fclose(f);
    }

    f = fopen("/proc/net/dev", "r");
    if (f) {
        char line[256];
        unsigned long rx_bytes = 0, tx_bytes = 0;

        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return;
        }
        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return;
        }

        while (fgets(line, sizeof(line), f)) {
            char iface[64];
            unsigned long r_bytes, t_bytes;
            unsigned long dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7;

            if (sscanf(line, "%63[^:]:%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                      iface, &r_bytes, &dummy1, &dummy2, &dummy3, &dummy4,
                      &dummy5, &dummy6, &dummy7, &t_bytes, &dummy1) >= 10) {

                if (strcmp(iface, "lo") != 0) {
                    rx_bytes += r_bytes;
                    tx_bytes += t_bytes;
                }
            }
        }

        stats.network_rx_kbps = (double)rx_bytes / 1024.0 / interval_;
        stats.network_tx_kbps = (double)tx_bytes / 1024.0 / interval_;

        fclose(f);
    }
}

void CRxSampleThread::emit_alert(const SRxSystemStats& stats,
                                 const CRxSampleModule* module,
                                 bool cpu_hit,
                                 bool mem_hit,
                                 bool net_hit)
{
    shared_ptr<SRxSampleMsg> msg(new SRxSampleMsg());
    msg->stats = stats;
    msg->cpu_hit = cpu_hit;
    msg->mem_hit = mem_hit;
    msg->net_hit = net_hit;

    if (module) {
        msg->module_name = module->name;
        msg->capture_hint = module->capture_hint;
        msg->capture_category = module->capture_category;
        msg->capture_duration_sec = module->capture_duration_sec;
        msg->cooldown_sec = module->cooldown_sec;
        msg->cpu_threshold = module->cpu_pct_gt;
        msg->mem_threshold = module->mem_pct_gt;
        msg->net_threshold = module->net_rx_kbps_gt;
    } else {
        msg->module_name = "default";
        msg->cpu_threshold = thr_.cpu_pct_gt;
        msg->mem_threshold = thr_.mem_pct_gt;
        msg->net_threshold = thr_.net_rx_kbps_gt;

        CRxStrategyConfigManager* cfg = CRxProcData::instance()->current_strategy_config();
        if (cfg) {
            msg->capture_duration_sec = cfg->get_default_duration();
            msg->capture_category = cfg->get_default_category();
        }
    }

    CRxProcData* pdata = CRxProcData::instance();
    if (pdata) {
        CRxCaptureManagerThread* manager = pdata->get_capture_manager_thread();
        if (manager) {
            shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(msg);
            ObjId target;
            target._id = OBJ_ID_THREAD;
            target._thread_index = manager->get_thread_index();
            base_net_thread::put_obj_msg(target, base);
        }
    }

    LOG_NOTICE_MSG("Sample threshold exceeded [module=%s]: CPU=%d MEM=%d NET=%d",
                   msg->module_name.c_str(),
                   cpu_hit ? 1 : 0,
                   mem_hit ? 1 : 0,
                   net_hit ? 1 : 0);
}

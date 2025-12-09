#ifndef __RX_RELOAD_THREAD_H__
#define __RX_RELOAD_THREAD_H__

#include "legacy_core.h"
#include "rxprocdata.h"

#define TIMER_TYPE_RELOAD_CONF 100

class CRxReloadThread:public base_net_thread
{
    public:
        CRxReloadThread();

        virtual void run_process();

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg);

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

        void reload_timer_start();

    private:
        bool _is_first;
        uint32_t _reload_interval_ms;

        /* Helper: write detected endian back to PDEF file */
        void writeback_pdef_endian(const char* pdef_file_path, int detected_endian);
};

#endif

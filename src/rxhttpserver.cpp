#include "rxhttpserver.h"
#include "rxhttpthread.h"
#include "rxserverconfig.h"
#include "rxprocdata.h"
#include "legacy_core.h"

#include <cstdio>

CRxHttpServer::CRxHttpServer(CRxServerConfig* conf)
    : _conf(conf)
{
}

CRxHttpServer::~CRxHttpServer()
{
}

void CRxHttpServer::start()
{
    if (!_conf) {
        LOG_FATAL_MSG("CRxHttpServer start failed: config is NULL");
        return;
    }


    LOG_NOTICE("Initializing capture and management threads...");
    if (CRxProcData::instance()->init_threads() < 0) {
        LOG_FATAL_MSG("Failed to initialize capture threads");
        return;
    }

    listen_thread * lthread = new (std::nothrow)listen_thread();
    lthread->init(_conf->bind_addr(), _conf->port());
    CRxProcData::instance()->add_name_thread("listen_thread", lthread);

    int works = _conf->workers()? _conf->workers() : 1;
    for (int i = 0; i < works; i++) {
        CRxHttpResThread* net_thread = new (std::nothrow) CRxHttpResThread();
        if (!net_thread) {
            LOG_FATAL_MSG("Failed to allocate HTTP worker %d", i);
            continue;
        }

        if (net_thread->name().empty()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "http_res_%d", i);
            net_thread->set_name(buf);
        }

        lthread->add_worker_thread(net_thread->get_thread_index());

        CRxProcData::instance()->add_name_thread("http_res", net_thread);
        net_thread->start();
    }

    lthread->start();
    base_net_thread::join_all_thread();
}

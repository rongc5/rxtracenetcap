#include "rxhttpthread.h"
#include "rxhttpresdataprocess.h"
#include "legacy_core.h"
#include "rxprocdata.h"
#include "rxsamplethread.h"


using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

#include <unistd.h>
#include <cstdio>

CRxHttpResThread::CRxHttpResThread()
    : base_net_thread(1)
{
}

CRxHttpResThread::~CRxHttpResThread()
{
    stop();
}

void CRxHttpResThread::handle_msg(shared_ptr<normal_msg>& msg)
{
    if (!msg) {
        return;
    }

    switch (msg->_msg_op) {
        case NORMAL_MSG_CONNECT:
        {
            shared_ptr<content_msg> conn = dynamic_pointer_cast<content_msg>(msg);
            if (conn) {
                handle_connect(conn);
            }
            break;
        }
        case RX_MSG_CAP_STARTED:
        {
            shared_ptr<SRxCaptureStartedMsg> started =
                dynamic_pointer_cast<SRxCaptureStartedMsg>(msg);
            if (started) {
                on_capture_started(started);
            }
            break;
        }
        case RX_MSG_CAP_FINISHED:
        {
            shared_ptr<SRxCaptureFinishedMsg> finished =
                dynamic_pointer_cast<SRxCaptureFinishedMsg>(msg);
            if (finished) {
                on_capture_finished(finished);
            }
            break;
        }
        case RX_MSG_SAMPLE_TRIGGER:
        {
            shared_ptr<SRxSampleMsg> sample =
                dynamic_pointer_cast<SRxSampleMsg>(msg);
            if (sample) {
                handle_sample_alert(sample);
            }
            break;
        }
        default:
            base_net_thread::handle_msg(msg);
            break;
    }
}

void CRxHttpResThread::handle_connect(shared_ptr<content_msg>& msg)
{
    common_obj_container* container = get_net_container();
    if (!container) {
        close(msg->fd);
        return;
    }

    shared_ptr<base_connect<http_res_process> > connect(new base_connect<http_res_process>(msg->fd));
    http_res_process* proc = new http_res_process(connect);
    CRxHttpResDataProcess* data_proc = new CRxHttpResDataProcess(proc, this);
    proc->set_process(data_proc);
    connect->set_process(proc);
    connect->set_net_container(container);

    shared_ptr<timer_msg> t_msg(new timer_msg);
    t_msg->_timer_type = NONE_DATA_TIMER_TYPE;
    t_msg->_time_length = 30000;
    t_msg->_obj_id = connect->get_id()._id;
    connect->add_timer(t_msg);
}

void CRxHttpResThread::handle_reply(shared_ptr<SRxHttpReplyMsg>& msg)
{
    common_obj_container* container = get_net_container();
    if (!container) {
        return;
    }

    shared_ptr<base_net_obj> obj = container->find(msg->conn_id);
    if (!obj) {
        return;
    }

    shared_ptr<base_connect<http_res_process> > conn =
        dynamic_pointer_cast<base_connect<http_res_process> >(obj);
    if (!conn) {
        return;
    }

    http_res_process* proc = conn->process();
    if (!proc) {
        return;
    }

    CRxHttpResDataProcess* dp = dynamic_cast<CRxHttpResDataProcess*>(proc->get_process());
    if (!dp) {
        return;
    }

    dp->send_async_response(msg->status, msg->reason, msg->body, msg->headers);
}

void CRxHttpResThread::on_capture_started(const shared_ptr<SRxCaptureStartedMsg>& msg)
{
    if (!msg) {
        return;
    }
    LOG_NOTICE_MSG("Capture %d started at timestamp %lld",
                   msg->capture_id,
                   static_cast<long long>(msg->start_timestamp));
}

void CRxHttpResThread::on_capture_finished(const shared_ptr<SRxCaptureFinishedMsg>& msg)
{
    if (!msg) {
        return;
    }
    LOG_NOTICE_MSG("Capture %d finished: code=%d, packets=%lu, file=%s",
                   msg->capture_id,
                   msg->exit_code,
                   msg->packet_count,
                   msg->final_filepath.c_str());
}

void CRxHttpResThread::handle_sample_alert(const shared_ptr<SRxSampleMsg>& msg)
{
    if (!msg) {
        return;
    }

    const SRxSystemStats& stats = msg->stats;
    LOG_NOTICE_MSG("Sample alert id=%llu module=%s cpu_hit=%d mem_hit=%d net_hit=%d "
                   "cpu=%.2f%% mem=%.2f%% rx=%.2fKB/s tx=%.2fKB/s capture_hint=%s duration=%d",
                   static_cast<unsigned long long>(msg->alert_id),
                   msg->module_name.c_str(),
                   msg->cpu_hit ? 1 : 0,
                   msg->mem_hit ? 1 : 0,
                   msg->net_hit ? 1 : 0,
                   stats.cpu_percent,
                   stats.memory_percent,
                   stats.network_rx_kbps,
                   stats.network_tx_kbps,
                   msg->capture_hint.c_str(),
                   msg->capture_duration_sec);
}

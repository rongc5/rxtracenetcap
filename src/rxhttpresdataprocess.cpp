#include "rxhttpresdataprocess.h"
#include "rxhttpthread.h"
#include "rxstrategyconfig.h"
#include "legacy_core.h"
#include "rxprocdata.h"


CRxHttpResDataProcess::CRxHttpResDataProcess(http_base_process* process,
                                             CRxHttpResThread* owner)
    : http_base_data_process(process)
    , current_handler_()
{
    (void)owner;
}

std::string* CRxHttpResDataProcess::get_send_body(int& result)
{
    result = 1;
    std::string* body = new std::string(send_body_);
    send_body_.clear();
    return body;
}

std::string* CRxHttpResDataProcess::get_send_head()
{
    if (async_response_pending()) {
        LOG_DEBUG_MSG("HTTP send_head suppressed: waiting async reply");
        return NULL;
    }
    std::string* out = new std::string;
    http_res_head_para& res = _base_process->get_res_head_para();
    res.to_head_str(out);
    return out;
}

size_t CRxHttpResDataProcess::process_recv_body(const char* buf, size_t len, int& result)
{
    recv_body_.append(buf, len);
    result = 1;
    return len;
}

ObjId CRxHttpResDataProcess::connection_id()
{
    shared_ptr<base_net_obj> net_obj = get_base_net();
    return net_obj ? net_obj->get_id() : ObjId();
}

void CRxHttpResDataProcess::fill_response_headers(int status,
                                                  const std::string& reason,
                                                  const std::map<std::string,std::string>& headers,
                                                  size_t body_size)
{
    http_res_head_para& res = _base_process->get_res_head_para();
    res._response_code = status;
    res._response_str = reason;
    res._headers.clear();
    for (std::map<std::string,std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        res._headers.insert(*it);
    }
    char lenbuf[64];
    snprintf(lenbuf, sizeof(lenbuf), "%zu", body_size);
    res._headers["Content-Length"] = lenbuf;
    res._headers["Connection"] = "close";
}

void CRxHttpResDataProcess::send_async_response(int status,
                                                const std::string& reason,
                                                const std::string& body,
                                                const std::map<std::string,std::string>& headers)
{
    send_body_ = body;
    fill_response_headers(status, reason, headers, send_body_.size());
    set_async_response_pending(false);
    _base_process->notify_send_ready();
}

void CRxHttpResDataProcess::msg_recv_finish()
{
    build_request();

    http_req_head_para& req_head = _base_process->get_req_head_para();

    current_handler_.reset();

    shared_ptr<CRxUrlHandler> handler = CRxProcData::instance()->get_url_handler(req_head._url_path);
    if (!handler) {
        handler = CRxProcData::instance()->get_url_handler("/default");
    }

    set_async_response_pending(false);
    if (handler) {
        current_handler_ = handler;
        http_res_head_para& res_head = _base_process->get_res_head_para();
        std::string body_response;

        ObjId conn_id = connection_id();
        bool is_ready = current_handler_->perform(&req_head, &recv_body_, &res_head, &body_response, conn_id);
        LOG_DEBUG_MSG("HTTP handler %s ready=%d conn=%u thread=%u",
                      req_head._url_path.c_str(),
                      is_ready ? 1 : 0,
                      conn_id._id,
                      conn_id._thread_index);

        if (is_ready) {

            send_body_ = body_response;
            _base_process->notify_send_ready();
        }
        else {
            set_async_response_pending(true);
        }

    }
}

void CRxHttpResDataProcess::handle_msg(shared_ptr<normal_msg>& p_msg)
{
    if (!p_msg) {
        return;
    }

    if (p_msg->_msg_op == NORMAL_MSG_HTTP_REPLY) {
        shared_ptr<SRxHttpReplyMsg> reply_msg =
            dynamic_pointer_cast<SRxHttpReplyMsg>(p_msg);
        if (reply_msg) {
            uint64_t now_ms = GetMilliSecond();
            if (reply_msg->debug_request_ts_ms > 0) {
                uint64_t manager_ms = 0;
                if (reply_msg->debug_reply_ts_ms >= reply_msg->debug_request_ts_ms) {
                    manager_ms = reply_msg->debug_reply_ts_ms - reply_msg->debug_request_ts_ms;
                }
                uint64_t dispatch_ms = 0;
                if (now_ms >= reply_msg->debug_reply_ts_ms && reply_msg->debug_reply_ts_ms > 0) {
                    dispatch_ms = now_ms - reply_msg->debug_reply_ts_ms;
                }
                uint64_t total_ms = now_ms - reply_msg->debug_request_ts_ms;
                LOG_NOTICE_MSG("HTTP async reply: conn=%u total=%llu ms (manager_queue=%llu ms dispatch=%llu ms)",
                               reply_msg->conn_id,
                               static_cast<unsigned long long>(total_ms),
                               static_cast<unsigned long long>(manager_ms),
                               static_cast<unsigned long long>(dispatch_ms));
            }
            send_async_response(reply_msg->status,
                              reply_msg->reason,
                              reply_msg->body,
                              reply_msg->headers);
        }
    }
}

void CRxHttpResDataProcess::build_request()
{

}

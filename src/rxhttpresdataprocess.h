#ifndef RX_HTTP_RES_DATA_PROCESS_H
#define RX_HTTP_RES_DATA_PROCESS_H

#include "legacy_core.h"
#include <map>
#include <string>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

struct SRxAppCtx;
class CRxHttpResThread;
class CRxUrlHandler;

class CRxHttpResDataProcess : public http_base_data_process {
public:
    CRxHttpResDataProcess(http_base_process* process,
                          CRxHttpResThread* owner);
    virtual ~CRxHttpResDataProcess() {}

    virtual std::string* get_send_body(int& result);
    virtual std::string* get_send_head();
    virtual size_t process_recv_body(const char* buf, size_t len, int& result);
    virtual void msg_recv_finish();
    virtual void handle_msg(shared_ptr<normal_msg>& p_msg);

    void send_async_response(int status,
                             const std::string& reason,
                             const std::string& body,
                             const std::map<std::string,std::string>& headers);
    ObjId connection_id();

private:
    void build_request();
    void fill_response_headers(int status,
                               const std::string& reason,
                               const std::map<std::string,std::string>& headers,
                               size_t body_size);

private:
    std::string recv_body_;
    std::string send_body_;
    shared_ptr<CRxUrlHandler> current_handler_;
};

#endif

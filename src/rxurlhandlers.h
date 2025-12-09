#ifndef RXTRACENETCAP_URL_HANDLERS_H
#define RXTRACENETCAP_URL_HANDLERS_H

#include "rxurlhandler.h"

#include <string>
#include "legacy_core.h"
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

struct http_req_head_para;
struct http_res_head_para;
struct ObjId;
class normal_msg;

class CRxUrlHandlerStaticJson : public CRxUrlHandler {
public:
    CRxUrlHandlerStaticJson(int code, const std::string& reason, const std::string& body);

    virtual bool perform(http_req_head_para* req_head,
                         std::string* recv_body,
                         http_res_head_para* res_head,
                         std::string* send_body,
                         const ObjId& conn_id);

private:
    int code_;
    std::string reason_;
    std::string body_;
};

class CRxUrlHandlerCaptureApi : public CRxUrlHandler {
public:
    CRxUrlHandlerCaptureApi();

    virtual bool perform(http_req_head_para* req_head,
                         std::string* recv_body,
                         http_res_head_para* res_head,
                         std::string* send_body,
                         const ObjId& conn_id);

private:
    bool handle_start(http_req_head_para* req_head,
                      std::string* recv_body,
                      http_res_head_para* res_head,
                      std::string* send_body,
                      const ObjId& conn_id);

    bool handle_stop(http_req_head_para* req_head,
                     std::string* recv_body,
                     http_res_head_para* res_head,
                     std::string* send_body,
                     const ObjId& conn_id);

    bool handle_status(http_req_head_para* req_head,
                       std::string* recv_body,
                       http_res_head_para* res_head,
                       std::string* send_body,
                       const ObjId& conn_id);

    bool send_to_capture_manager(shared_ptr<normal_msg> msg,
                                 http_res_head_para* res_head,
                                 std::string* send_body,
                                 const ObjId& conn_id);

    void set_error_response(http_res_head_para* res_head,
                            std::string* send_body,
                            int code,
                            const std::string& message);
};

class CRxUrlHandlerPdefUpload : public CRxUrlHandler {
public:
    CRxUrlHandlerPdefUpload();

    virtual bool perform(http_req_head_para* req_head,
                         std::string* recv_body,
                         http_res_head_para* res_head,
                         std::string* send_body,
                         const ObjId& conn_id);
};

class CRxUrlHandlerPdefManagement : public CRxUrlHandler {
public:
    CRxUrlHandlerPdefManagement();

    virtual bool perform(http_req_head_para* req_head,
                         std::string* recv_body,
                         http_res_head_para* res_head,
                         std::string* send_body,
                         const ObjId& conn_id);

private:
    bool handle_list(http_req_head_para* req_head,
                     std::string* recv_body,
                     http_res_head_para* res_head,
                     std::string* send_body,
                     const ObjId& conn_id);

    bool handle_get(http_req_head_para* req_head,
                    std::string* recv_body,
                    http_res_head_para* res_head,
                    std::string* send_body,
                    const ObjId& conn_id);

    void set_json_response(http_res_head_para* res_head,
                          std::string* send_body,
                          int code,
                          const std::string& reason,
                          const std::string& json_body);

    void set_error_response(http_res_head_para* res_head,
                           std::string* send_body,
                           int code,
                           const std::string& message);

    std::string format_mtime(time_t mtime);
    bool is_safe_path(const std::string& path);
};

#endif

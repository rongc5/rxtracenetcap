#ifndef RX_URL_HANDLER_H
#define RX_URL_HANDLER_H

#include <string>

struct http_req_head_para;
struct http_res_head_para;
struct ObjId;

class CRxUrlHandler {
public:
    virtual ~CRxUrlHandler() {}

    virtual bool perform(http_req_head_para* req_head,
                         std::string* recv_body,
                         http_res_head_para* res_head,
                         std::string* send_body,
                         const ObjId& conn_id) = 0;
};

#endif

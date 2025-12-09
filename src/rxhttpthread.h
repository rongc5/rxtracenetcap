#ifndef RX_HTTP_THREAD_H
#define RX_HTTP_THREAD_H

#include "legacy_core.h"
#include "rxmsgtypes.h"
#include "rxcapturemessages.h"

#include <string>
#include <vector>
using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

struct SRxSampleMsg;

class CRxHttpResThread : public base_net_thread {
public:
    CRxHttpResThread();
    ~CRxHttpResThread();

    void set_name(const std::string& n) { name_ = n; }
    const std::string& name() const { return name_; }

protected:
    virtual void handle_msg(shared_ptr<normal_msg>& msg);

private:
    void handle_connect(shared_ptr<content_msg>& msg);
    void handle_reply(shared_ptr<SRxHttpReplyMsg>& msg);
    void on_capture_started(const shared_ptr<SRxCaptureStartedMsg>& msg);
    void on_capture_finished(const shared_ptr<SRxCaptureFinishedMsg>& msg);
    void handle_sample_alert(const shared_ptr<SRxSampleMsg>& msg);

private:
    std::string name_;
};

#endif

#ifndef RX_HTTP_SERVER_H
#define RX_HTTP_SERVER_H

#include "legacy_core.h"

class CRxServerConfig;

class CRxHttpServer {
public:
    explicit CRxHttpServer(CRxServerConfig* conf);
    virtual ~CRxHttpServer();

    void start();
    void stop();

private:
    CRxServerConfig* _conf;
};

#endif

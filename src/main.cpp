#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string>

#include "legacy_core.h"
#include "rxstrategyconfig.h"
#include "rxserverconfig.h"
#include "rxprocdata.h"
#include "rxhttpserver.h"

static void do_init()
{
    char proc_name[SIZE_LEN_128];
    get_proc_name(proc_name, sizeof(proc_name));

    std::string conf_path("./conf/");
    conf_path += proc_name;
    conf_path += ".ini";

    CRxServerConfig * conf = new (std::nothrow)CRxServerConfig(conf_path.c_str());

    CRxProcData::instance()->init(conf);
    CRxProcData::instance()->load();

    LOG_INIT(conf->log_path);

    CRxHttpServer * http_server = new (std::nothrow)CRxHttpServer(conf);
    http_server->start();
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    do_init();

    return 0;
}

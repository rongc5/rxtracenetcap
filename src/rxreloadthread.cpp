#include "rxreloadthread.h"
#include <malloc.h>
#include "rxprocdata.h"
#include "legacy_core.h"
#include "rxmsgtypes.h"
#include "pdef/pdef_types.h"
#include <sys/file.h>
#include <fcntl.h>
#include <time.h>
#include <string>

using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;
#include "rxstrategyconfig.h"

CRxReloadThread::CRxReloadThread()
    : _is_first(false)
    , _reload_interval_ms(60000)
{
}

void CRxReloadThread::run_process()
{
    if (!_is_first)
    {
        _is_first = true;
        reload_timer_start();
    }
}

void CRxReloadThread::handle_msg(shared_ptr<normal_msg> & p_msg)
{
    CRxProcData* p_data = CRxProcData::instance();
    if (!p_data || !p_msg)
        return;

    switch(p_msg->_msg_op)
    {
        case RX_MSG_PDEF_ENDIAN_DETECTED:
        {

            shared_ptr<SRxPdefEndianMsg> endian_msg = dynamic_pointer_cast<SRxPdefEndianMsg>(p_msg);
            if (endian_msg) {
                LOG_NOTICE("Received PDEF endian writeback request: %s -> %s",
                           endian_msg->pdef_file_path,
                           endian_msg->detected_endian == ENDIAN_TYPE_BIG ? "big" : "little");

                writeback_pdef_endian(endian_msg->pdef_file_path, endian_msg->detected_endian);
            }
            break;
        }

        default:
        {
            LOG_DEBUG("Unknown message operation: %d", p_msg->_msg_op);
        }
        break;
    }
}

void CRxReloadThread::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    LOG_DEBUG("handle_timeout timer_type:%d, time_length:%d", t_msg->_timer_type, t_msg->_time_length);
    CRxProcData* p_data = CRxProcData::instance();
    if (t_msg->_timer_type == TIMER_TYPE_RELOAD_CONF && p_data)
    {
        if (p_data->reload())
        {
        }

        {
            reload_timer_start();
        }

        malloc_trim(0);
    }
}

void CRxReloadThread::reload_timer_start()
{
    CRxProcData* p_data = CRxProcData::instance();
    (void)p_data;


    _reload_interval_ms = 1000;

    shared_ptr<timer_msg> t_msg(new timer_msg);
    t_msg->_timer_type = TIMER_TYPE_RELOAD_CONF;
    t_msg->_time_length = _reload_interval_ms;
    t_msg->_obj_id = OBJ_ID_THREAD;
    add_timer(t_msg);
}

void CRxReloadThread::writeback_pdef_endian(const char* pdef_file_path, int detected_endian)
{
    if (!pdef_file_path || pdef_file_path[0] == '\0') {
        LOG_ERROR("Invalid PDEF file path for writeback");
        return;
    }

    const char* endian_str = (detected_endian == ENDIAN_TYPE_BIG) ? "big" : "little";


    int fd = open(pdef_file_path, O_RDWR);
    if (fd < 0) {
        LOG_WARNING("Failed to open PDEF file for writeback: %s (insufficient permissions?)",
                    pdef_file_path);
        return;
    }


    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        LOG_WARNING("Failed to lock PDEF file (another process is writing): %s", pdef_file_path);
        close(fd);
        return;
    }


    std::string content;
    char buf[4096];
    ssize_t nread;
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        content.append(buf, nread);
    }


    if (content.find("endian ") != std::string::npos) {
        LOG_NOTICE("PDEF file already has endian configuration, skipping writeback: %s",
                   pdef_file_path);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }


    size_t protocol_pos = content.find("protocol ");
    if (protocol_pos == std::string::npos) {
        LOG_ERROR("Invalid PDEF file format (no 'protocol' keyword): %s", pdef_file_path);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    size_t brace_pos = content.find("{", protocol_pos);
    if (brace_pos == std::string::npos) {
        LOG_ERROR("Invalid PDEF file format (no '{' after 'protocol'): %s", pdef_file_path);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }


    time_t now = time(NULL);
    char timestamp[64];
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_buf);


    char endian_line[256];
    snprintf(endian_line, sizeof(endian_line),
             "\n    endian %s;  # auto-detected on %s\n",
             endian_str, timestamp);


    std::string new_content = content.substr(0, brace_pos + 1);
    new_content += endian_line;
    new_content += content.substr(brace_pos + 1);


    if (ftruncate(fd, 0) != 0) {
        LOG_ERROR("Failed to truncate PDEF file: %s", pdef_file_path);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    lseek(fd, 0, SEEK_SET);
    ssize_t written = write(fd, new_content.c_str(), new_content.length());
    if (written < 0 || (size_t)written != new_content.length()) {
        LOG_ERROR("Failed to write PDEF file: %s", pdef_file_path);
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }


    fsync(fd);


    flock(fd, LOCK_UN);
    close(fd);

    LOG_NOTICE("Successfully wrote endian configuration to PDEF file: %s -> %s",
               pdef_file_path, endian_str);
}

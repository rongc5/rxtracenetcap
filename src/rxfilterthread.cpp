#include "rxfilterthread.h"
#include "runtime/protocol.h"
#include "rxstorageutils.h"
#include "rxprocdata.h"
#include "rxreloadthread.h"
#include <unistd.h>
#include <stdio.h>

namespace {
    const int RX_THREAD_FILTER_TYPE = 5;
}

CRxFilterThread::CRxFilterThread()
    : base_net_thread(1)
    , protocol_def_(NULL)
    , dump_ctx_(NULL)
    , type_(RX_THREAD_FILTER_TYPE)
    , name_("filter")
{
}

CRxFilterThread::~CRxFilterThread()
{
    LOG_NOTICE("Filter thread destructing, stats: processed=%lu matched=%lu filtered=%lu",
               stats_.packets_processed,
               stats_.packets_matched,
               stats_.packets_filtered);
}

bool CRxFilterThread::init(ProtocolDef* protocol_def, CRxDumpCtx* dump_ctx)
{
    if (!dump_ctx) {
        LOG_ERROR("Filter thread init failed: invalid dump context");
        return false;
    }

    protocol_def_ = protocol_def;
    dump_ctx_ = dump_ctx;
    return true;
}

bool CRxFilterThread::start()
{
    if (!dump_ctx_) {
        LOG_ERROR("Filter thread not initialized, call init() first");
        return false;
    }

    if (!base_net_thread::start()) {
        LOG_ERROR("Failed to start filter thread");
        return false;
    }

    LOG_NOTICE("Filter thread started successfully");
    return true;
}

void CRxFilterThread::handle_msg(shared_ptr<normal_msg>& p_msg)
{
    if (!p_msg) {
        return;
    }

    if (p_msg->_msg_op == RX_MSG_PACKET_CAPTURED) {
        /* Received packet from capture thread */
        shared_ptr<SRxPacketMsg> packet_msg = dynamic_pointer_cast<SRxPacketMsg>(p_msg);
        if (packet_msg) {
            handle_packet_captured(packet_msg);
        }
    } else {
        /* Unknown message, pass to base class */
        base_net_thread::handle_msg(p_msg);
    }
}

void CRxFilterThread::handle_packet_captured(shared_ptr<SRxPacketMsg>& packet_msg)
{
    stats_.packets_processed++;

    /* Check endian before filter (to detect changes) */
    int endian_before = ENDIAN_TYPE_UNKNOWN;
    if (protocol_def_ && protocol_def_->endian_mode == ENDIAN_MODE_AUTO) {
        __sync_synchronize();  /* Memory barrier */
        endian_before = protocol_def_->detected_endian;
    }

    /* Apply PDEF filter */
    bool matched = apply_filter(packet_msg.get());

    /* Check if endian was auto-detected during this filter */
    if (protocol_def_ && protocol_def_->endian_mode == ENDIAN_MODE_AUTO) {
        __sync_synchronize();  /* Memory barrier */
        int endian_after = protocol_def_->detected_endian;

        if (endian_before == ENDIAN_TYPE_UNKNOWN && endian_after != ENDIAN_TYPE_UNKNOWN) {
            /* Endian was just detected! Send writeback message to reload thread (once) */
            /* Use GCC built-in CAS for C++98 compatibility */
            int old_val = __sync_val_compare_and_swap(&protocol_def_->endian_writeback_done,
                                                       0, 1);
            if (old_val == 0) {
                /* We are the first to send the writeback message */
                CRxProcData* proc_data = CRxProcData::instance();
                CRxReloadThread* reload_thread = proc_data ? proc_data->get_reload_thread() : NULL;

                if (reload_thread && protocol_def_->pdef_file_path[0] != '\0') {
                    shared_ptr<SRxPdefEndianMsg> msg = make_shared<SRxPdefEndianMsg>();
                    strncpy(msg->pdef_file_path, protocol_def_->pdef_file_path,
                            sizeof(msg->pdef_file_path) - 1);
                    msg->detected_endian = endian_after;

                    /* Send message to reload thread */
                    ObjId target;
                    target._id = OBJ_ID_THREAD;
                    target._thread_index = reload_thread->get_thread_index();

                    shared_ptr<normal_msg> base_msg = static_pointer_cast<normal_msg>(msg);
                    base_net_thread::put_obj_msg(target, base_msg);

                    LOG_NOTICE("Sent PDEF endian writeback request: %s -> %s",
                               protocol_def_->pdef_file_path,
                               endian_after == ENDIAN_TYPE_BIG ? "big" : "little");
                }
            }
        }
    }

    if (matched) {
        stats_.packets_matched++;

        /* Write packet to file directly */
        write_packet(packet_msg.get());
    } else {
        stats_.packets_filtered++;
    }
}

bool CRxFilterThread::apply_filter(const SRxPacketMsg* packet)
{
    if (!packet || !packet->valid) {
        return false;
    }

    /* If no protocol filter is configured, accept all packets */
    if (!protocol_def_) {
        return true;
    }

    /* Apply PDEF filter to application layer data */
    if (packet->app_len == 0) {
        return false;
    }

    const uint8_t* app_data = packet->data + packet->app_offset;

    /* Try matching with destination port first */
    bool matched = packet_filter_match(
        app_data,
        packet->app_len,
        packet->dst_port,
        protocol_def_
    );

    if (!matched) {
        /* Also try source port (for reverse direction) */
        matched = packet_filter_match(
            app_data,
            packet->app_len,
            packet->src_port,
            protocol_def_
        );
    }

    return matched;
}

void CRxFilterThread::write_packet(const SRxPacketMsg* packet)
{
    if (!packet || !dump_ctx_ || !dump_ctx_->d) {
        return;
    }

    long pkt_bytes = (long)sizeof(struct pcap_pkthdr) + (long)packet->header.caplen;

    /* Check if we need to rotate to a new file */
    if (dump_ctx_->max_bytes > 0 &&
        dump_ctx_->written + pkt_bytes > dump_ctx_->max_bytes) {
        CRxStorageUtils::rotate_open(dump_ctx_);
        if (!dump_ctx_->d) {
            LOG_ERROR("Failed to rotate pcap file");
            return;
        }
    }

    /* Write packet to pcap file */
    pcap_dump((u_char*)dump_ctx_->d, &packet->header, packet->data);
    dump_ctx_->written += pkt_bytes;
}

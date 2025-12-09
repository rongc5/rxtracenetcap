#ifndef RX_FILTER_THREAD_H
#define RX_FILTER_THREAD_H

#include "legacy_core.h"
#include "rxlockfreequeue.h"
#include "pdef/pdef_types.h"
#include "rxmsgtypes.h"
#include "rxstorageutils.h"
#include <string>
#include <pcap.h>

using compat::shared_ptr;
using compat::weak_ptr;
using compat::static_pointer_cast;
using compat::dynamic_pointer_cast;
using compat::const_pointer_cast;
using compat::make_shared;

/*
 * Packet message for inter-thread communication
 */
struct SRxPacketMsg : public normal_msg {
    struct pcap_pkthdr header;
    uint8_t data[65536];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t app_offset;  /* offset to app data within 'data' */
    uint32_t app_len;
    bool valid;
    uint32_t writer_thread_index;  /* target writer thread */

    SRxPacketMsg() : normal_msg(RX_MSG_PACKET_CAPTURED),
                     src_port(0), dst_port(0),
                     app_offset(0), app_len(0),
                     valid(false), writer_thread_index(0)
    {
        memset(&header, 0, sizeof(header));
        memset(data, 0, sizeof(data));
    }
};

/*
 * Filter thread - receives packet messages from capture thread,
 * applies PDEF filtering (with sliding window support),
 * and forwards matched packets to writer thread via messages.
 *
 * Inherits from base_net_thread like other threads in the system.
 */
class CRxFilterThread : public base_net_thread {
public:
    CRxFilterThread();
    ~CRxFilterThread();

    /**
     * Initialize filter thread
     * @param protocol_def PDEF protocol definition
     * @param dump_ctx Dump context for writing packets to pcap files
     */
    bool init(ProtocolDef* protocol_def, CRxDumpCtx* dump_ctx);

    /**
     * Start the filter thread
     */
    bool start();

    /**
     * Get filter statistics
     */
    struct FilterStats {
        unsigned long packets_processed;
        unsigned long packets_matched;
        unsigned long packets_filtered;
        unsigned long queue_empty_count;
        unsigned long output_queue_full_count;

        FilterStats()
            : packets_processed(0)
            , packets_matched(0)
            , packets_filtered(0)
            , queue_empty_count(0)
            , output_queue_full_count(0)
        {}
    };

    FilterStats get_stats() const { return stats_; }
    void reset_stats() { stats_ = FilterStats(); }

protected:
    /**
     * Handle incoming messages (called by base_net_thread)
     */
    virtual void handle_msg(shared_ptr<normal_msg>& p_msg);

private:
    /**
     * Process a captured packet message
     */
    void handle_packet_captured(shared_ptr<SRxPacketMsg>& packet_msg);

    /**
     * Apply PDEF filter to packet
     */
    bool apply_filter(const SRxPacketMsg* packet);

    /**
     * Write packet to pcap file
     */
    void write_packet(const SRxPacketMsg* packet);

private:
    ProtocolDef* protocol_def_;
    CRxDumpCtx* dump_ctx_;
    FilterStats stats_;
    int type_;
    std::string name_;
};

#endif /* RX_FILTER_THREAD_H */

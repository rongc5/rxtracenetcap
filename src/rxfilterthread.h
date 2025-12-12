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

struct SRxCaptureRawFileMsgV2;

struct SRxPacketMsg : public normal_msg {
    struct pcap_pkthdr header;
    uint8_t data[65536];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t app_offset;
    uint32_t app_len;
    bool valid;
    uint32_t writer_thread_index;

    SRxPacketMsg() : normal_msg(RX_MSG_PACKET_CAPTURED),
                     src_port(0), dst_port(0),
                     app_offset(0), app_len(0),
                     valid(false), writer_thread_index(0)
    {
        memset(&header, 0, sizeof(header));
        memset(data, 0, sizeof(data));
    }
};

class CRxFilterThread : public base_net_thread {
public:
    CRxFilterThread();
    ~CRxFilterThread();

    bool init(ProtocolDef* protocol_def, CRxDumpCtx* dump_ctx);

    bool start();

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
    virtual void handle_msg(shared_ptr<normal_msg>& p_msg);

private:
    void handle_packet_captured(shared_ptr<SRxPacketMsg>& packet_msg);

    void handle_raw_file(shared_ptr<SRxCaptureRawFileMsgV2>& raw_msg);

    bool apply_filter(const SRxPacketMsg* packet);

    void write_packet(const SRxPacketMsg* packet);

    struct ParsedPacket {
        const uint8_t* app_data;
        uint32_t app_len;
        uint16_t src_port;
        uint16_t dst_port;
        bool valid;
    };
    ParsedPacket parse_packet_data(const uint8_t* data, uint32_t len);

    void send_endian_detected_to_manager(int manager_thread_index,
                                        const std::string& pdef_path,
                                        int detected_endian,
                                        int capture_id);

private:
    ProtocolDef* protocol_def_;
    CRxDumpCtx* dump_ctx_;
    FilterStats stats_;
    int type_;
    std::string name_;
};

#endif

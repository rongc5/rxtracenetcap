#include "rxfilterthread.h"
#include "runtime/protocol.h"
#include "rxstorageutils.h"
#include "rxprocdata.h"
#include "rxreloadthread.h"
#include "rxcapturemessages.h"
#include "pdef/parser.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

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
    if (!base_net_thread::start()) {
        LOG_ERROR("Failed to start filter thread");
        return false;
    }

    LOG_NOTICE("Filter thread started successfully, thread_index=%u", get_thread_index());
    return true;
}

void CRxFilterThread::handle_msg(shared_ptr<normal_msg>& p_msg)
{
    if (!p_msg) {
        return;
    }

    if (p_msg->_msg_op == RX_MSG_CAPTURE_RAW_FILE) {
        fprintf(stderr, "[DEBUG FILTER] Received RX_MSG_CAPTURE_RAW_FILE message\n");

        shared_ptr<SRxCaptureRawFileMsgV2> raw_msg =
            dynamic_pointer_cast<SRxCaptureRawFileMsgV2>(p_msg);
        if (raw_msg) {
            fprintf(stderr, "[DEBUG FILTER] Calling handle_raw_file() for capture_id=%d\n", raw_msg->capture_id);
            handle_raw_file(raw_msg);
        } else {
            fprintf(stderr, "[DEBUG FILTER] ERROR: Failed to cast message to SRxCaptureRawFileMsgV2\n");
        }
    } else if (p_msg->_msg_op == RX_MSG_PACKET_CAPTURED) {

        shared_ptr<SRxPacketMsg> packet_msg = dynamic_pointer_cast<SRxPacketMsg>(p_msg);
        if (packet_msg) {
            handle_packet_captured(packet_msg);
        }
    } else {

        base_net_thread::handle_msg(p_msg);
    }
}

void CRxFilterThread::handle_packet_captured(shared_ptr<SRxPacketMsg>& packet_msg)
{
    stats_.packets_processed++;


    int endian_before = ENDIAN_TYPE_UNKNOWN;
    if (protocol_def_ && protocol_def_->endian_mode == ENDIAN_MODE_AUTO) {
        __sync_synchronize();
        endian_before = protocol_def_->detected_endian;
    }


    bool matched = apply_filter(packet_msg.get());

    if (protocol_def_ && protocol_def_->endian_mode == ENDIAN_MODE_AUTO) {
        __sync_synchronize();
        int endian_after = protocol_def_->detected_endian;

        if (endian_before == ENDIAN_TYPE_UNKNOWN && endian_after != ENDIAN_TYPE_UNKNOWN) {


            int old_val = __sync_val_compare_and_swap(&protocol_def_->endian_writeback_done,
                                                       0, 1);
            if (old_val == 0) {

                CRxProcData* proc_data = CRxProcData::instance();
                CRxReloadThread* reload_thread = proc_data ? proc_data->get_reload_thread() : NULL;

                if (reload_thread && protocol_def_->pdef_file_path[0] != '\0') {
                    shared_ptr<SRxPdefEndianMsg> msg = make_shared<SRxPdefEndianMsg>();
                    snprintf(msg->pdef_file_path, sizeof(msg->pdef_file_path), "%s",
                             protocol_def_->pdef_file_path);
                    msg->detected_endian = endian_after;


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


    if (!protocol_def_) {
        return true;
    }


    if (packet->app_len == 0) {
        return false;
    }

    const uint8_t* app_data = packet->data + packet->app_offset;


    bool matched = packet_filter_match(
        app_data,
        packet->app_len,
        packet->dst_port,
        protocol_def_
    );

    if (!matched) {

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


    if (dump_ctx_->max_bytes > 0 &&
        dump_ctx_->written + pkt_bytes > dump_ctx_->max_bytes) {
        CRxStorageUtils::rotate_open(dump_ctx_);
        if (!dump_ctx_->d) {
            LOG_ERROR("Failed to rotate pcap file");
            return;
        }
    }


    pcap_dump((u_char*)dump_ctx_->d, &packet->header, packet->data);
    dump_ctx_->written += pkt_bytes;
}

CRxFilterThread::ParsedPacket CRxFilterThread::parse_packet_data(const uint8_t* data, uint32_t len)
{
    ParsedPacket result;
    result.app_data = NULL;
    result.app_len = 0;
    result.src_port = 0;
    result.dst_port = 0;
    result.valid = false;
    const uint32_t eth_header_len = 14u;


    if (len < eth_header_len) return result;

    uint16_t eth_type = (data[12] << 8) | data[13];
    if (eth_type != 0x0800) return result;


    const uint8_t* ip_header = data + eth_header_len;
    if (len < eth_header_len + 20u) return result;

    uint32_t ip_header_len = static_cast<uint32_t>((ip_header[0] & 0x0F) * 4);
    uint8_t protocol = ip_header[9];

    if (len < eth_header_len + ip_header_len) return result;


    const uint8_t* transport_header = ip_header + ip_header_len;
    if (len < eth_header_len + ip_header_len + 4u) return result;

    result.src_port = (transport_header[0] << 8) | transport_header[1];
    result.dst_port = (transport_header[2] << 8) | transport_header[3];

    uint32_t transport_header_len = 0;
    if (protocol == 6) {
        if (len < eth_header_len + ip_header_len + 12u) return result;
        transport_header_len = static_cast<uint32_t>(((transport_header[12] >> 4) & 0x0F) * 4);
    } else if (protocol == 17) {
        transport_header_len = 8u;
    } else {
        return result;
    }


    uint32_t offset = eth_header_len + ip_header_len + transport_header_len;
    if (offset >= len) return result;

    result.app_data = transport_header + transport_header_len;
    result.app_len = len - offset;
    result.valid = (result.app_len > 0);

    return result;
}

void CRxFilterThread::handle_raw_file(shared_ptr<SRxCaptureRawFileMsgV2>& raw_msg)
{
    fprintf(stderr, "[DEBUG FILTER RAW] handle_raw_file() started\n");

    if (!raw_msg || !raw_msg->has_pdef_filter) {
        fprintf(stderr, "[DEBUG FILTER RAW] ERROR: Invalid raw_msg or no pdef filter\n");
        LOG_WARNING("FilterThread: received invalid raw file message");
        return;
    }

    fprintf(stderr, "[DEBUG FILTER RAW] Processing %s\n", raw_msg->raw_pcap_path.c_str());

    LOG_NOTICE("FilterThread %u: processing %s",
               get_thread_index(), raw_msg->raw_pcap_path.c_str());

    int64_t start_ts = rx_capture_now_usec();


    ProtocolDef* pdef = NULL;
    char errmsg[512];

    if (!raw_msg->pdef_inline_content.empty()) {
        fprintf(stderr, "[DEBUG FILTER RAW] Parsing inline PDEF\n");
        pdef = pdef_parse_string(raw_msg->pdef_inline_content.c_str(), errmsg, sizeof(errmsg));
    } else if (!raw_msg->pdef_file_path.empty()) {
        fprintf(stderr, "[DEBUG FILTER RAW] Parsing PDEF file: %s\n", raw_msg->pdef_file_path.c_str());
        pdef = pdef_parse_file(raw_msg->pdef_file_path.c_str(), errmsg, sizeof(errmsg));
    }

    if (!pdef) {
        fprintf(stderr, "[DEBUG FILTER RAW] ERROR: Failed to load PDEF: %s\n", errmsg);
        LOG_ERROR("FilterThread: failed to load PDEF: %s", errmsg);

        return;
    }

    fprintf(stderr, "[DEBUG FILTER RAW] PDEF loaded successfully\n");


    char pcap_errbuf[PCAP_ERRBUF_SIZE];
    fprintf(stderr, "[DEBUG FILTER RAW] Opening raw pcap: %s\n", raw_msg->raw_pcap_path.c_str());
    pcap_t* pcap_in = pcap_open_offline(raw_msg->raw_pcap_path.c_str(), pcap_errbuf);
    if (!pcap_in) {
        fprintf(stderr, "[DEBUG FILTER RAW] ERROR: Failed to open raw pcap: %s\n", pcap_errbuf);
        LOG_ERROR("FilterThread: failed to open raw pcap: %s", pcap_errbuf);
        protocol_free(pdef);
        return;
    }

    fprintf(stderr, "[DEBUG FILTER RAW] Raw pcap opened successfully\n");

    std::string filtered_path = raw_msg->raw_pcap_path;
    size_t pos = filtered_path.find("_raw.pcap");
    if (pos != std::string::npos) {
        filtered_path.replace(pos, 9, ".pcap");
    } else {
        filtered_path += ".filtered";
    }

    fprintf(stderr, "[DEBUG FILTER RAW] Creating filtered output: %s\n", filtered_path.c_str());

    pcap_dumper_t* pcap_out = pcap_dump_open(pcap_in, filtered_path.c_str());
    if (!pcap_out) {
        fprintf(stderr, "[DEBUG FILTER RAW] ERROR: Failed to open output pcap: %s\n", pcap_geterr(pcap_in));
        LOG_ERROR("FilterThread: failed to open output pcap: %s", pcap_geterr(pcap_in));
        pcap_close(pcap_in);
        protocol_free(pdef);
        return;
    }

    fprintf(stderr, "[DEBUG FILTER RAW] Output pcap created, starting filtering...\n");


    struct pcap_pkthdr* header;
    const u_char* data;
    unsigned long total = 0;
    unsigned long matched = 0;
    int ret;

    while ((ret = pcap_next_ex(pcap_in, &header, &data)) > 0) {
        total++;

        ParsedPacket parsed = parse_packet_data(data, header->caplen);
        if (!parsed.valid || parsed.app_len == 0) {
            continue;
        }

        bool match = packet_filter_match(
            parsed.app_data,
            parsed.app_len,
            parsed.dst_port,
            pdef
        );

        if (!match) {
            match = packet_filter_match(
                parsed.app_data,
                parsed.app_len,
                parsed.src_port,
                pdef
            );
        }

        if (match) {
            pcap_dump((u_char*)pcap_out, header, data);
            matched++;
        }


        if (total % 10000 == 0) {
            LOG_DEBUG("FilterThread %u: processed %lu packets, %lu matched",
                      get_thread_index(), total, matched);
        }
    }


    fprintf(stderr, "[DEBUG FILTER RAW] Filtering complete: %lu/%lu packets matched. Closing files...\n", matched, total);
    pcap_dump_close(pcap_out);
    pcap_close(pcap_in);

    int64_t finish_ts = rx_capture_now_usec();
    double elapsed_sec = (finish_ts - start_ts) / 1000000.0;

    fprintf(stderr, "[DEBUG FILTER RAW] Files closed, elapsed time: %.2f sec\n", elapsed_sec);

    LOG_NOTICE("FilterThread %u: filtered %s in %.2f sec (%lu/%lu packets kept)",
               get_thread_index(), raw_msg->raw_pcap_path.c_str(),
               elapsed_sec, matched, total);


    if (unlink(raw_msg->raw_pcap_path.c_str()) != 0) {
        LOG_WARNING("FilterThread: failed to delete raw file: %s (errno=%d)",
                    raw_msg->raw_pcap_path.c_str(), errno);
    } else {
        fprintf(stderr, "[DEBUG FILTER RAW] Deleted raw file: %s\n", raw_msg->raw_pcap_path.c_str());
    }


    fprintf(stderr, "[DEBUG FILTER RAW] Final filtered file: %s\n", filtered_path.c_str());


    shared_ptr<SRxCaptureFilteredFileMsgV2> filtered(new SRxCaptureFilteredFileMsgV2());
    filtered->capture_id = raw_msg->capture_id;
    filtered->key = raw_msg->key;
    filtered->sid = raw_msg->sid;
    filtered->sender_thread_index = static_cast<int>(get_thread_index());
    filtered->filtered_pcap_path = filtered_path;
    filtered->total_packets = total;
    filtered->filtered_packets = matched;
    filtered->pdef_file_path = raw_msg->pdef_file_path;


    struct stat st;
    if (stat(filtered_path.c_str(), &st) == 0) {
        filtered->file_size = static_cast<unsigned long>(st.st_size);
    }


    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(raw_msg->manager_thread_index);
    shared_ptr<normal_msg> base_msg = static_pointer_cast<normal_msg>(filtered);
    base_net_thread::put_obj_msg(target, base_msg);


    if (pdef->endian_mode == ENDIAN_MODE_AUTO &&
        pdef->detected_endian != ENDIAN_TYPE_UNKNOWN) {

        std::string pdef_path = !raw_msg->pdef_file_path.empty() ?
                                raw_msg->pdef_file_path : "";

        if (!pdef_path.empty()) {
            LOG_NOTICE("FilterThread %u: detected endian=%s for %s (capture_id=%d)",
                      get_thread_index(),
                      pdef->detected_endian == ENDIAN_TYPE_BIG ? "big" : "little",
                      pdef_path.c_str(),
                      raw_msg->capture_id);


            send_endian_detected_to_manager(
                raw_msg->manager_thread_index,
                pdef_path,
                pdef->detected_endian,
                raw_msg->capture_id
            );
        }
    }


    protocol_free(pdef);
}

void CRxFilterThread::send_endian_detected_to_manager(
    int manager_thread_index,
    const std::string& pdef_path,
    int detected_endian,
    int capture_id)
{
    shared_ptr<SRxPdefEndianMsg> msg(new SRxPdefEndianMsg());


    strncpy(msg->pdef_file_path, pdef_path.c_str(), sizeof(msg->pdef_file_path) - 1);
    msg->pdef_file_path[sizeof(msg->pdef_file_path) - 1] = '\0';

    msg->detected_endian = detected_endian;
    msg->capture_id = capture_id;


    ObjId target;
    target._id = OBJ_ID_THREAD;
    target._thread_index = static_cast<uint32_t>(manager_thread_index);

    shared_ptr<normal_msg> base = static_pointer_cast<normal_msg>(msg);
    base_net_thread::put_obj_msg(target, base);

    LOG_DEBUG("FilterThread %u: sent endian detection to manager (thread_index=%d)",
              get_thread_index(), manager_thread_index);
}

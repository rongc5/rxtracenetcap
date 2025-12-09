#include "rxstorageutils.h"

#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>

// PDEF includes
#include "pdef/parser.h"
#include "runtime/protocol.h"

// Filter thread includes
#include "legacy_core.h"
#include "rxfilterthread.h"

using compat::shared_ptr;
using compat::make_shared;
using compat::static_pointer_cast;

// Ethernet header size
#define ETHER_HEADER_LEN 14
#define ETHER_TYPE_IP  0x0800
#define ETHER_TYPE_IP6 0x86DD

// IP protocol numbers (use system defines if available)
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

// Helper struct to hold parsed packet information
struct ParsedPacket {
    const uint8_t* app_data;
    uint32_t app_len;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t ip_proto;
    bool valid;
};

// Parse packet to extract application layer data and ports
// Supports Ethernet -> IPv4 -> TCP/UDP
static ParsedPacket parse_packet(const u_char* packet, uint32_t packet_len) {
    ParsedPacket result;
    result.app_data = NULL;
    result.app_len = 0;
    result.src_port = 0;
    result.dst_port = 0;
    result.ip_proto = 0;
    result.valid = false;

    if (packet_len < ETHER_HEADER_LEN) {
        return result;
    }

    // Parse Ethernet header
    uint16_t ether_type = (packet[12] << 8) | packet[13];

    const uint8_t* ip_packet = packet + ETHER_HEADER_LEN;
    uint32_t remaining = packet_len - ETHER_HEADER_LEN;

    // Only support IPv4 for now
    if (ether_type != ETHER_TYPE_IP) {
        return result;
    }

    if (remaining < 20) {  // Minimum IP header size
        return result;
    }

    // Parse IPv4 header
    uint8_t ip_version = (ip_packet[0] >> 4) & 0x0F;
    if (ip_version != 4) {
        return result;
    }

    uint8_t ip_hdr_len = (ip_packet[0] & 0x0F) * 4;  // IHL field * 4 bytes
    if (ip_hdr_len < 20 || remaining < ip_hdr_len) {
        return result;
    }

    uint8_t ip_proto = ip_packet[9];
    result.ip_proto = ip_proto;

    const uint8_t* transport = ip_packet + ip_hdr_len;
    uint32_t transport_remaining = remaining - ip_hdr_len;

    // Parse TCP or UDP header
    if (ip_proto == IPPROTO_TCP) {
        if (transport_remaining < 20) {  // Minimum TCP header size
            return result;
        }

        result.src_port = (transport[0] << 8) | transport[1];
        result.dst_port = (transport[2] << 8) | transport[3];

        uint8_t tcp_hdr_len = ((transport[12] >> 4) & 0x0F) * 4;
        if (tcp_hdr_len < 20 || transport_remaining < tcp_hdr_len) {
            return result;
        }

        result.app_data = transport + tcp_hdr_len;
        result.app_len = transport_remaining - tcp_hdr_len;
        result.valid = true;

    } else if (ip_proto == IPPROTO_UDP) {
        if (transport_remaining < 8) {  // UDP header size
            return result;
        }

        result.src_port = (transport[0] << 8) | transport[1];
        result.dst_port = (transport[2] << 8) | transport[3];

        result.app_data = transport + 8;
        result.app_len = transport_remaining - 8;
        result.valid = true;
    }

    return result;
}

std::string CRxStorageUtils::two_digits(int v)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d", v);
    return std::string(b);
}

std::string CRxStorageUtils::ymd_date(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d",
        tmv.tm_year + 1900,
        tmv.tm_mon + 1,
        tmv.tm_mday,
        tmv.tm_hour,
        tmv.tm_min);

    return std::string(buf);
}

void CRxStorageUtils::ensure_dir(const std::string& path)
{

    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        cur.push_back(c);
        if (c == '/' && cur.size() > 1) {
            mkdir(cur.c_str(), 0755);
        }
    }
}

std::string CRxStorageUtils::join_path(const std::string& a, const std::string& b)
{
    if (b.empty()) {
        return a;
    }
    if (!b.empty() && b[0] == '/') {
        return b;
    }
    if (a.empty()) {
        return b;
    }
    if (a[a.size() - 1] == '/') {
        return a + b;
    }
    return a + "/" + b;
}

std::string CRxStorageUtils::expand_pattern(const CRxDumpCtx* dc)
{

    std::string pat = dc->pattern.empty() ?
        std::string("{day}/{date}-{iface}-{proc}-{port}.pcap") : dc->pattern;

    std::string out;
    out.reserve(pat.size() + 64);

    time_t base_time = (dc->start_time != 0) ? dc->start_time : time(NULL);
    struct tm tmv;
    localtime_r(&base_time, &tmv);

    char tsbuf[32];
    snprintf(tsbuf, sizeof(tsbuf), "%lu", (unsigned long)base_time);

    char daybuf[32];
    snprintf(daybuf, sizeof(daybuf), "%04d%02d%02d",
        tmv.tm_year + 1900,
        tmv.tm_mon + 1,
        tmv.tm_mday);

    char dtbuf[32];
    snprintf(dtbuf, sizeof(dtbuf), "%04d%02d%02d%02d%02d",
        tmv.tm_year + 1900,
        tmv.tm_mon + 1,
        tmv.tm_mday,
        tmv.tm_hour,
        tmv.tm_min);

    bool has_port_placeholder = false;

    for (size_t i = 0; i < pat.size();) {
        if (pat[i] == '{') {
            size_t j = pat.find('}', i + 1);
            if (j == std::string::npos) {
                out.push_back(pat[i++]);
                continue;
            }

            std::string key = pat.substr(i + 1, j - (i + 1));
            if (key == "day") {
                out += daybuf;
            } else if (key == "date") {
                out += dtbuf;
            } else if (key == "ts") {
                out += tsbuf;
            } else if (key == "iface") {
                out += dc->iface;
            } else if (key == "proc") {
                if (!dc->proc.empty()) {
                    out += dc->proc;
                } else {
                    out += "any";
                }
            } else if (key == "port") {
                has_port_placeholder = true;
                if (dc->port > 0) {
                    char b[16];
                    snprintf(b, sizeof(b), "%d", dc->port);
                    out += b;
                } else {
                    out += "any";
                }
            } else if (key == "seq") {
                char b[16];
                snprintf(b, sizeof(b), "%04d", dc->seq);
                out += b;
            } else if (key == "category") {
                if (!dc->category.empty()) {
                    out += dc->category;
                }
            } else {
                out += key;
            }
            i = j + 1;
        } else {
            out.push_back(pat[i++]);
        }
    }

    if (out.find("//") != std::string::npos) {
        std::string collapsed;
        collapsed.reserve(out.size());
        bool prev_slash = false;
        for (size_t k = 0; k < out.size(); ++k) {
            char c = out[k];
            if (c == '/') {
                if (!prev_slash) {
                    collapsed.push_back(c);
                    prev_slash = true;
                }
                continue;
            }
            collapsed.push_back(c);
            prev_slash = false;
        }
        out.swap(collapsed);
    }

    if (dc->port > 0 && !has_port_placeholder) {
        char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", dc->port);
        size_t dot_pos = out.rfind('.');
        if (dot_pos != std::string::npos) {
            out.insert(dot_pos, std::string("-p") + port_buf);
        } else {
            out.append("-p");
            out.append(port_buf);
        }
    }

    return join_path(dc->base_dir, out);
}

void CRxStorageUtils::rotate_open(CRxDumpCtx* dc)
{
    if (dc->d) {
        pcap_dump_flush(dc->d);
        pcap_dump_close(dc->d);

        if (dc->compress_enabled && !dc->current_path.empty()) {

            std::string cmd = dc->compress_cmd.empty() ?
                std::string("gzip -9 ") : dc->compress_cmd + " ";
            cmd += dc->current_path;
            int rc = system(cmd.c_str());
            if (rc != 0) {
                fprintf(stderr, "compression command failed rc=%d for %s\n", rc, dc->current_path.c_str());
            }
        }
    }

    dc->seq += 1;
    dc->written = 0;
    dc->current_path = expand_pattern(dc);

    size_t pos = dc->current_path.find_last_of('/');
    if (pos != std::string::npos) {
        ensure_dir(dc->current_path.substr(0, pos + 1));
    }

    dc->d = pcap_dump_open(dc->p, dc->current_path.c_str());
}

void CRxStorageUtils::dump_cb(u_char* user, const struct pcap_pkthdr* h, const u_char* bytes)
{
    CRxDumpCtx* dc = (CRxDumpCtx*)user;

    // Unified message-driven architecture: always use filter/writer thread if available
    if (dc->filter_thread_index > 0) {
        // Parse packet to extract application layer data
        ParsedPacket parsed = parse_packet(bytes, h->caplen);

        // Create message
        shared_ptr<SRxPacketMsg> msg = make_shared<SRxPacketMsg>();
        memcpy(&msg->header, h, sizeof(struct pcap_pkthdr));
        memcpy(msg->data, bytes, h->caplen);
        msg->src_port = parsed.src_port;
        msg->dst_port = parsed.dst_port;
        msg->app_offset = (uint32_t)(parsed.app_data ? (parsed.app_data - bytes) : 0);
        msg->app_len = parsed.app_len;
        msg->valid = parsed.valid;

        // Send to filter/writer thread (will filter if PDEF is set, otherwise write directly)
        ObjId target;
        target._id = OBJ_ID_THREAD;
        target._thread_index = dc->filter_thread_index;

        shared_ptr<normal_msg> base_msg = static_pointer_cast<normal_msg>(msg);
        base_net_thread::put_obj_msg(target, base_msg);
        return;
    }

    // Fallback: synchronous write in capture thread (only if filter thread creation failed)
    long pkt_bytes = (long)sizeof(struct pcap_pkthdr) + (long)h->caplen;

    // Check if we need to rotate to a new file
    if (dc->max_bytes > 0 && dc->d && dc->written + pkt_bytes > dc->max_bytes) {
        rotate_open(dc);
        if (!dc->d) {
            return;
        }
    }

    // Save the packet directly
    pcap_dump((u_char*)dc->d, h, bytes);
    dc->written += pkt_bytes;
}

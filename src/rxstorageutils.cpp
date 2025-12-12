#include "rxstorageutils.h"

#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>


#include "pdef/parser.h"
#include "runtime/protocol.h"


#include "legacy_core.h"
#include "rxfilterthread.h"

using compat::shared_ptr;
using compat::make_shared;
using compat::static_pointer_cast;


#define ETHER_HEADER_LEN 14
#define ETHER_TYPE_IP  0x0800
#define ETHER_TYPE_IP6 0x86DD


#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

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


    if (!dc->protocol_filter_path.empty()) {
        fprintf(stderr, "[DEBUG] Adding _raw suffix because protocol_filter_path='%s'\n",
                dc->protocol_filter_path.c_str());
        size_t dot_pos = out.rfind('.');
        if (dot_pos != std::string::npos) {
            out.insert(dot_pos, "_raw");
        } else {
            out.append("_raw");
        }
    } else {
        fprintf(stderr, "[DEBUG] No protocol_filter_path, not adding _raw suffix\n");
    }

    return join_path(dc->base_dir, out);
}

void CRxStorageUtils::rotate_open(CRxDumpCtx* dc)
{
    if (dc->d) {
        pcap_dump_flush(dc->d);
        pcap_dump_close(dc->d);

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

    long pkt_bytes = (long)sizeof(struct pcap_pkthdr) + (long)h->caplen;

    if (dc->max_bytes > 0 && dc->d && dc->written + pkt_bytes > dc->max_bytes) {
        rotate_open(dc);
        if (!dc->d) {
            return;
        }
    }

    pcap_dump((u_char*)dc->d, h, bytes);
    dc->written += pkt_bytes;
}

#include "rxcapturesession.h"
#include <time.h>
#include <stdio.h>
#include <unistd.h>


#include "pdef/parser.h"
#include "runtime/protocol.h"

static unsigned long now_sec()
{
    return (unsigned long)time(NULL);
}

CRxCaptureJob::CRxCaptureJob(const CRxCaptureTaskCfg& cfg, const CRxCaptureTaskInfo* parent_task_info)
    : cfg_(cfg), parent_task_info_(parent_task_info), pcap_handle_(NULL), done_(false), packets_(0), end_time_sec_(0),
      filter_thread_(NULL), use_filter_thread_(false)
{
}

CRxCaptureJob::~CRxCaptureJob()
{

    if (pcap_handle_) {
        cleanup();
    }
}

bool CRxCaptureJob::prepare()
{
    char errbuf[PCAP_ERRBUF_SIZE];
    errbuf[0] = '\0';

    pcap_handle_ = pcap_open_live(cfg_.iface.c_str(), 65535, 1, 1000, errbuf);
    if (!pcap_handle_) {
        fprintf(stderr, "pcap_open_live failed for %s: %s\n", cfg_.iface.c_str(), errbuf);
        return false;
    }

    if (!cfg_.bpf.empty()) {
        struct bpf_program bpf;
        if (pcap_compile(pcap_handle_, &bpf, cfg_.bpf.c_str(), 1, PCAP_NETMASK_UNKNOWN) == 0) {
            pcap_setfilter(pcap_handle_, &bpf);
            pcap_freecode(&bpf);
        } else {
            fprintf(stderr, "pcap_compile failed for BPF: %s\n", cfg_.bpf.c_str());

        }
    }

    errbuf[0] = '\0';
    if (pcap_setnonblock(pcap_handle_, 1, errbuf) == -1) {
        fprintf(stderr, "pcap_setnonblock failed for %s: %s\n", cfg_.iface.c_str(),
                errbuf[0] ? errbuf : "unknown error");

    }

    dumper_context_.p = pcap_handle_;
    dumper_context_.d = NULL;
    dumper_context_.max_bytes = cfg_.max_bytes;
    dumper_context_.seq = 0;
    dumper_context_.written = 0;
    dumper_context_.start_time = time(NULL);
    dumper_context_.base_dir = parent_task_info_->base_dir;
    dumper_context_.pattern = cfg_.file_pattern;
    dumper_context_.category = cfg_.category;
    dumper_context_.iface = cfg_.iface;
    dumper_context_.proc = cfg_.proc_name;
    dumper_context_.port = cfg_.port;
    dumper_context_.compress_enabled = parent_task_info_->compress_enabled;


    dumper_context_.protocol_filter_path = cfg_.protocol_filter;
    dumper_context_.protocol_def = NULL;
    dumper_context_.packets_filtered = 0;
    dumper_context_.filter_thread_index = 0;


    use_filter_thread_ = false;
    filter_thread_ = NULL;

    fprintf(stderr, "[Capture] Direct write mode (PDEF filtering will be done offline if needed)\n");

    if (!cfg_.file_pattern.empty() || !parent_task_info_->base_dir.empty()) {
        CRxStorageUtils::rotate_open(&dumper_context_);
        if (!dumper_context_.d) {
            fprintf(stderr, "pcap_dump_open failed (pattern): %s\n", pcap_geterr(pcap_handle_));
            pcap_close(pcap_handle_);
            pcap_handle_ = NULL;
            return false;
        }
    } else if (!cfg_.outfile.empty()) {
        dumper_context_.d = pcap_dump_open(pcap_handle_, cfg_.outfile.c_str());
        dumper_context_.current_path = cfg_.outfile;
        if (!dumper_context_.d) {
            fprintf(stderr, "pcap_dump_open failed for %s: %s\n", cfg_.outfile.c_str(), pcap_geterr(pcap_handle_));
            pcap_close(pcap_handle_);
            pcap_handle_ = NULL;
            return false;
        }
    } else {
        fprintf(stderr, "No output file or pattern specified\n");
        pcap_close(pcap_handle_);
        pcap_handle_ = NULL;
        return false;
    }

    if (cfg_.duration_sec > 0) {
        end_time_sec_ = now_sec() + (unsigned long)cfg_.duration_sec;
    }

    return true;
}

int CRxCaptureJob::run_once()
{
    if (is_done()) {
        return -2;
    }

    int ret = pcap_dispatch(pcap_handle_, 100, CRxStorageUtils::dump_cb, (u_char*)&dumper_context_);

    if (ret > 0) {
        packets_ += (unsigned long)ret;
    }

    if (ret == 0) {
        usleep(1000);
    } else if (ret < 0) {
        usleep(1000);
    }

    if (parent_task_info_->stopping || (end_time_sec_ > 0 && now_sec() >= end_time_sec_)) {
        done_ = true;
    }

    return ret;
}

void CRxCaptureJob::cleanup()
{

    if (filter_thread_) {
        fprintf(stderr, "[Filter] Stopping filter/writer thread...\n");
        filter_thread_->stop();
        filter_thread_->join_thread();


        CRxFilterThread::FilterStats stats = filter_thread_->get_stats();
        if (dumper_context_.protocol_def) {
            fprintf(stderr, "[Filter] Thread stats: processed=%lu matched=%lu filtered=%lu\n",
                    stats.packets_processed, stats.packets_matched, stats.packets_filtered);
        } else {
            fprintf(stderr, "[Filter] Thread stats: processed=%lu written=%lu\n",
                    stats.packets_processed, stats.packets_matched);
        }

        delete filter_thread_;
        filter_thread_ = NULL;
    }


    if (dumper_context_.protocol_def) {
        fprintf(stderr, "[PDEF] Filtered %lu packets (did not match protocol filter)\n",
                dumper_context_.packets_filtered);

        protocol_free(dumper_context_.protocol_def);
        dumper_context_.protocol_def = NULL;
    }

    if (dumper_context_.d) {
        pcap_dump_flush(dumper_context_.d);
        pcap_dump_close(dumper_context_.d);
        dumper_context_.d = NULL;
    }
    if (pcap_handle_) {
        pcap_close(pcap_handle_);
        pcap_handle_ = NULL;
    }

    done_ = true;
}

bool CRxCaptureJob::is_done() const
{
    return done_;
}

unsigned long CRxCaptureJob::get_packet_count() const
{
    return packets_;
}

std::string CRxCaptureJob::get_final_path() const
{
    return dumper_context_.current_path;
}

std::string CRxCaptureJob::get_current_file() const
{
    return dumper_context_.current_path;
}

unsigned long CRxCaptureJob::get_bytes_written() const
{
    if (dumper_context_.written < 0) {
        return 0;
    }
    return static_cast<unsigned long>(dumper_context_.written);
}

uint32_t CRxCaptureJob::get_filter_thread_index() const
{
    if (filter_thread_) {
        return filter_thread_->get_thread_index();
    }
    return 0;
}

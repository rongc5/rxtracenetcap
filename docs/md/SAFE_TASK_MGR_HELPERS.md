// SafeTaskMgr 便捷方法扩展建议
// 这些方法应该在 Manager 线程中使用，以简化常见的状态更新操作

/**
 * ========================================================================
 * 便捷方法扩展（建议添加到 SafeTaskMgr 类中）
 * ========================================================================
 */

// 将任务标记为已启动
inline bool set_capture_started(int capture_id, long start_ts, int worker_pid)
{
    TaskSnapshot snapshot;
    if (!query_task(capture_id, snapshot)) {
        return false;  // 任务不存在
    }

    SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
    new_task->status = STATUS_RUNNING;
    new_task->start_time = start_ts;
    new_task->capture_pid = worker_pid;

    return update_task(capture_id, new_task);
}

// 更新任务进度
inline bool update_progress(int capture_id, long packets, long bytes, long last_ts)
{
    TaskSnapshot snapshot;
    if (!query_task(capture_id, snapshot)) {
        return false;
    }

    SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
    new_task->packet_count = packets;
    new_task->bytes_captured = bytes;
    // 可以添加更多进度相关字段

    return update_task(capture_id, new_task);
}

// 将任务标记为已完成
inline bool set_capture_finished(int capture_id, long finish_ts, 
                                 long total_packets, long total_bytes)
{
    TaskSnapshot snapshot;
    if (!query_task(capture_id, snapshot)) {
        return false;
    }

    SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
    new_task->status = STATUS_COMPLETED;
    new_task->end_time = finish_ts;
    new_task->packet_count = total_packets;
    new_task->bytes_captured = total_bytes;

    return update_task(capture_id, new_task);
}

// 将任务标记为失败
inline bool set_capture_failed(int capture_id, int error_code, 
                               const std::string& error_msg)
{
    TaskSnapshot snapshot;
    if (!query_task(capture_id, snapshot)) {
        return false;
    }

    SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
    new_task->status = STATUS_FAILED;
    new_task->error_message = error_msg;
    new_task->end_time = time(NULL);

    return update_task(capture_id, new_task);
}

// 添加输出文件路径
inline bool add_output_file(int capture_id, const std::string& file_path)
{
    TaskSnapshot snapshot;
    if (!query_task(capture_id, snapshot)) {
        return false;
    }

    SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
    new_task->output_file = file_path;

    return update_task(capture_id, new_task);
}

/**
 * ========================================================================
 * 使用示例
 * ========================================================================
 */

class CRxCaptureManagerV3 : public CRxBaseNetThread {
private:
    SafeTaskMgr task_mgr_;

public:
    // 处理 Worker 发来的 "抓包已启动" 消息
    void handle_capture_started(SRxCaptureStartedMsg* msg) {
        task_mgr_.set_capture_started(msg->capture_id, msg->start_ts_usec, msg->worker_pid);
        // 可选：转发给 HTTP 线程
        notify_http_thread(RX_MSG_CAP_STARTED, msg);
    }

    // 处理 Worker 发来的 "进度更新" 消息
    void handle_capture_progress(SRxCaptureProgressMsg* msg) {
        task_mgr_.update_progress(msg->capture_id, 
                                  msg->progress.packets,
                                  msg->progress.bytes,
                                  msg->progress.last_packet_ts_usec);
        // 可选：按条件转发给 HTTP 线程（如每 10 个进度消息转发 1 个）
    }

    // 处理 Worker 发来的 "文件就绪" 消息
    void handle_capture_file_ready(SRxCaptureFileReadyMsg* msg) {
        // 处理批量文件
        for (size_t i = 0; i < msg->files.size(); ++i) {
            task_mgr_.add_output_file(msg->capture_id, msg->files[i].file_path);
            
            // 立即转发给 Clean 线程处理
            SRxFileEnqueueMsg* enqueue_msg = new SRxFileEnqueueMsg();
            enqueue_msg->capture_id = msg->capture_id;
            enqueue_msg->file_path = msg->files[i].file_path;
            enqueue_msg->file_size = msg->files[i].file_size;
            
            clean_thread_->add_msg(enqueue_msg);
        }
    }

    // 处理 Worker 发来的 "抓包完成" 消息
    void handle_capture_finished(SRxCaptureFinishedMsg* msg) {
        task_mgr_.set_capture_finished(msg->capture_id,
                                       msg->result.finish_ts_usec,
                                       msg->result.total_packets,
                                       msg->result.total_bytes);
        // 转发给 HTTP 线程
        notify_http_thread(RX_MSG_CAP_FINISHED, msg);
    }

    // 处理 Worker 发来的 "抓包失败" 消息
    void handle_capture_failed(SRxCaptureFailedMsg* msg) {
        task_mgr_.set_capture_failed(msg->capture_id,
                                     msg->error_code,
                                     msg->error_msg);
        // 转发给 HTTP 线程
        notify_http_thread(RX_MSG_CAP_FAILED, msg);
    }

    // 处理 Clean 线程发来的 "压缩完成" 消息
    void handle_clean_compress_done(SRxCleanCompressDoneMsg* msg) {
        // 更新任务状态（例如标记为已归档）
        TaskSnapshot snapshot;
        if (task_mgr_.query_task(msg->capture_id, snapshot)) {
            SRxCaptureTask* new_task = new SRxCaptureTask(*snapshot);
            // 可以添加自定义状态字段，例如 archived_path
            task_mgr_.update_task(msg->capture_id, new_task);
        }
        
        // 转发给 HTTP 线程
        notify_http_thread(RX_MSG_CLEAN_COMPRESS_DONE, msg);
    }

private:
    void notify_http_thread(int msg_op, CRxMsg* msg) {
        // 将消息转发给 HTTP 线程
        if (http_peer_id_) {
            msg->op = msg_op;
            post_to(http_peer_id_, msg);
        }
    }
};

/**
 * ========================================================================
 * Worker 线程的消息发送示例
 * ========================================================================
 */

class CRxCaptureThread : public CRxBaseNetThread {
private:
    void run_job(SRxCaptureRequest* req) {
        // 1. 告诉 Manager：抓包已启动
        SRxCaptureStartedMsg started_msg;
        started_msg.capture_id = req->capture_id;
        started_msg.start_ts_usec = get_current_time_usec();
        started_msg.worker_pid = getpid();
        started_msg.applied_spec_hash = req->config.config_hash;
        
        manager_thread_->add_msg(&started_msg);

        // 2. 创建抓包 job
        CCaptureJob job(req->cfg, req->config);
        if (!job.prepare()) {
            // 告诉 Manager：启动失败
            SRxCaptureFailedMsg failed_msg;
            failed_msg.capture_id = req->capture_id;
            failed_msg.error_code = ERR_START_INVALID_PARAMS;
            failed_msg.error_msg = "Failed to prepare capture";
            manager_thread_->add_msg(&failed_msg);
            return;
        }

        // 3. 进度上报循环
        int64_t last_progress_report = get_current_time_usec();
        while (!job.is_done()) {
            int ret = job.run_once();
            
            if (ret > 0) {
                // 检查是否需要上报进度
                int64_t now_usec = get_current_time_usec();
                int progress_interval_usec = req->config.progress_interval_sec * 1000000LL;
                
                if (now_usec - last_progress_report >= progress_interval_usec) {
                    SRxCaptureProgressMsg progress_msg;
                    progress_msg.capture_id = req->capture_id;
                    progress_msg.progress = job.get_progress();
                    progress_msg.report_ts_usec = now_usec;
                    
                    manager_thread_->add_msg(&progress_msg);
                    last_progress_report = now_usec;
                }
            }
        }

        // 4. 告诉 Manager：文件就绪（批量）
        SRxCaptureFileReadyMsg file_msg;
        file_msg.capture_id = req->capture_id;
        // 可能有多个文件（分段），添加到 files 向量中
        // file_msg.files.push_back(...);
        manager_thread_->add_msg(&file_msg);

        // 5. 清理并报告最终结果
        job.cleanup();

        // 6. 告诉 Manager：抓包完成
        SRxCaptureFinishedMsg finished_msg;
        finished_msg.capture_id = req->capture_id;
        finished_msg.result.total_packets = job.get_packet_count();
        finished_msg.result.total_bytes = job.get_total_bytes();
        finished_msg.result.finish_ts_usec = get_current_time_usec();
        finished_msg.result.exit_code = 0;
        
        manager_thread_->add_msg(&finished_msg);
    }
};

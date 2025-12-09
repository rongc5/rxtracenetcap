#ifndef RXTRACENETCAP_LEGACY_CORE_NET_H
#define RXTRACENETCAP_LEGACY_CORE_NET_H

#ifndef RXTRACENETCAP_LEGACY_CORE_COMMON_INCLUDED
#error "Include legacy_core.h instead of legacy_core_net.h"
#endif

class base_data_process;
class common_obj_container;

class base_net_obj: public enable_shared_from_this<base_net_obj>
{
    public:
        base_net_obj();

        virtual ~base_net_obj();

        virtual void event_process(int events) = 0;

        bool get_real_net();
        void set_real_net(bool real_net);

        virtual int real_net_process()=0;

        virtual void set_net_container(common_obj_container *p_net_container);

        common_obj_container * get_net_container();

        virtual void update_event(int event);
        int get_event();

        virtual void notice_send();

        int get_sfd();

        void set_id(const ObjId & id_str);
        const ObjId & get_id();

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg);

        void add_timer(shared_ptr<timer_msg> & t_msg);
        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

        virtual void destroy();

        net_addr & get_peer_addr();

    protected:
        void add_timer();

    protected:
        common_obj_container *_p_net_container;
        int _epoll_event;
        int _fd;
        ObjId _id_str;
        bool _real_net;
        std::vector<shared_ptr<timer_msg> > _timer_vec;

        net_addr _peer_net;
};

class base_data_process
{
    public:
        base_data_process(shared_ptr<base_net_obj> p);

        virtual ~base_data_process();

        virtual void peer_close();

        virtual std::string *get_send_buf();

        virtual void reset();

        virtual size_t process_recv_buf(const char *buf, size_t len);

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg);

        void add_timer(shared_ptr<timer_msg> & t_msg);

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

        void put_send_buf(std::string * str);

        shared_ptr<base_net_obj>  get_base_net();

        virtual void destroy();

    protected:
        void clear_send_list();

    protected:
        weak_ptr<base_net_obj> _p_connect;
        std::list<std::string*> _send_list;
};

template<class PROCESS>
class base_connect:public base_net_obj
{
    public:

        base_connect(const int32_t sock)
        {
            _fd = sock;
            int bReuseAddr = 1;
            setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &bReuseAddr, sizeof(bReuseAddr));
            _p_send_buf = NULL;
            _process = NULL;
            get_peer_addr();
        }

        base_connect()
        {
            _p_send_buf = NULL;
            _process = NULL;
        }

        virtual ~base_connect()
        {
            if (_p_send_buf != NULL){
                delete _p_send_buf;
                _p_send_buf = NULL;
            }

            if (_process) {
                delete _process;
                _process = NULL;
            }
        }

        virtual void event_process(int event)
        {
            if ((event & EPOLLERR) == EPOLLERR || (event & EPOLLHUP) == EPOLLHUP)
            {
                THROW_COMMON_EXCEPT("epoll error "<< strError(errno).c_str());
            }

            if ((event & EPOLLIN) == EPOLLIN)
            {
                real_recv();
            }

            if ((event & EPOLLOUT) == EPOLLOUT )
            {
                real_send();
            }
        }
        virtual int real_net_process()
        {
            int32_t ret = 0;

            if ((get_event() & EPOLLIN) == EPOLLIN) {
                LOG_DEBUG("real_net_process real_recv");
                real_recv(true);
            }

            if ((get_event() & EPOLLOUT) == EPOLLOUT) {
                LOG_DEBUG("real_net_process real_send");
                real_send();
            }

            return ret;
        }

        virtual void destroy()
        {
            if (_process)
            {
                _process->destroy();
            }
        }

        PROCESS * process()
        {
            return _process;
        }

        void set_process(PROCESS *p)
        {
            if (_process != NULL)
            {
                _process->destroy();
                delete _process;
            }
            _process = p;
        }

        virtual void notice_send()
        {
            update_event(_epoll_event | EPOLLOUT);

            if (_process)
            {
                real_send();
            }
        }

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg)
        {
            if (t_msg->_timer_type == DELAY_CLOSE_TIMER_TYPE)
            {
                THROW_COMMON_EXCEPT("the connect obj delay close, delete it");
            }
            else if (t_msg->_timer_type == NONE_DATA_TIMER_TYPE)
            {
                THROW_COMMON_EXCEPT("the connect obj no data");
            }

            _process->handle_timeout(t_msg);
        }

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg)
        {
            _process->handle_msg(p_msg);
        }

    protected:
        virtual int RECV(void *buf, size_t len)
        {
            int ret = recv(_fd, buf, len, MSG_DONTWAIT);
            if (ret == 0)
            {
                _process->peer_close();
                THROW_COMMON_EXCEPT("the client close the socket(" << _fd << ")");
            }
            else if (ret < 0)
            {
                if (errno != EAGAIN)
                {
                    THROW_COMMON_EXCEPT("this socket occur fatal error " << strError(errno).c_str());
                }
                ret = 0;
            }

            return ret;
        }

        virtual ssize_t SEND(const void *buf, const size_t len)
        {
            if (len == 0)
            {
                THROW_COMMON_EXCEPT("close the socket " << _fd);
            }

            ssize_t ret =  send(_fd, buf, len, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (ret < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    THROW_COMMON_EXCEPT("send data error " << strError(errno).c_str());
                }
                LOG_WARNING("send error");
                ret = 0;
            }
            return ret;

        }

        void real_recv(int flag = false)
        {
            size_t _recv_buf_len = _recv_buf.length();
            size_t tmp_len = MAX_RECV_SIZE - _recv_buf_len;
            ssize_t ret = 0;
            size_t p_ret = 0;
            if (tmp_len > 0)
            {
                char t_buf[SIZE_LEN_32768];
                int r_len = tmp_len <= sizeof(t_buf) ? tmp_len:sizeof(t_buf);
                ret = RECV(t_buf, r_len);
                if (ret > 0){
                    _recv_buf.append(t_buf, ret);
                    _recv_buf_len += ret;
                }
            }

            if (_recv_buf_len > 0 || flag)
            {
                LOG_DEBUG("process_recv_buf _recv_buf_len[%d] fd[%d], flag[%d]", _recv_buf_len, _fd, flag);
                p_ret = _process->process_recv_buf(_recv_buf.data(), _recv_buf_len);
                if (p_ret && p_ret <= _recv_buf_len)
                {
                    _recv_buf.erase(0, p_ret);
                }
                else if (p_ret > _recv_buf_len)
                {
                    _recv_buf.erase(0, _recv_buf_len);
                }
                else
                {
                }
            }

            if (ret)
            {
                add_timer();
            }

            return;
        }

        void real_send()
        {
            std::string *p;
            while (_p_send_buf || (p = _process->get_send_buf()))
            {
                ssize_t _send_ret;
                if (NULL == _p_send_buf)
                {
                    _p_send_buf = p;
                    _send_num = 0;
                }
                ssize_t to_send = (_p_send_buf->size() - _send_num);
                if (to_send > 0)
                {
                    _send_ret = SEND(&((*_p_send_buf)[_send_num]), to_send);
                }

                if (_send_ret < 0)
                {
                    _process->destroy();
                }
                else if (_send_ret == 0)
                {
                    update_event(_epoll_event & (~EPOLLOUT));
                    break;
                }
                else
                {
                    _send_num += _send_ret;
                    if (_send_num >= (ssize_t)_p_send_buf->size())
                    {
                        delete _p_send_buf;
                        _p_send_buf = NULL;
                    }
                }
            }

            update_event(_epoll_event & (~EPOLLOUT));
        }

    protected:
        PROCESS * _process;
        std::string _recv_buf;
        std::string *_p_send_buf;
        ssize_t _send_num;
};

class normal_obj_msg
{
    public:
        uint32_t _id;
        shared_ptr<normal_msg>  p_msg;

        virtual ~normal_obj_msg(){
        }
};

class channel_data_process:public base_data_process
{
    public:
        channel_data_process(shared_ptr<base_net_obj> p, int channelid);

        virtual ~channel_data_process()
        {
            _queue[0].clear();
            _queue[1].clear();
        }

        virtual size_t process_recv_buf(const char *buf, size_t len);

        void put_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg);

        void add_event_timer(uint64_t time_out = MAX_CHANNEL_EVENT_TIMEOUT);

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

    protected:
        CRxThreadMutex _mutex;
        std::deque<normal_obj_msg> _queue[2];
        volatile int _current;
        int _channelid;
        uint64_t _last_time;
};

class base_timer
{
    public:
        base_timer(common_obj_container * net_container);

        virtual ~base_timer();

        uint32_t add_timer(shared_ptr<timer_msg> & t_msg);

        void check_timer(std::vector<uint32_t> &expect_list);

        bool is_empty();

        uint32_t gen_timerid();

    protected:
        std::multimap<uint64_t, shared_ptr<timer_msg> > _timer_list[2];
        std::set<uint32_t> _timerid_set;

        common_obj_container * _net_container;
        uint32_t _timerid;
        uint32_t _current;
};

class common_epoll
{
    public:
        common_epoll()
        {
            _epoll_events = NULL;
            _epoll_size = 0;
        }

        ~common_epoll()
        {
            if (_epoll_events != NULL)
                delete [] _epoll_events;
        }

        void init(const uint32_t epoll_size = DAFAULT_EPOLL_SIZE, int epoll_wait_time = DEFAULT_EPOLL_WAITE)
        {
            _epoll_size = (epoll_size == 0)?DAFAULT_EPOLL_SIZE:epoll_size;

            _epoll_wait_time = epoll_wait_time;

            _epoll_fd = epoll_create(_epoll_size);
            if (_epoll_fd == -1)
            {
                std::string err = strError(errno);
                THROW_COMMON_EXCEPT("epoll_create fail " << err.c_str());
            }
            _epoll_events = new epoll_event[_epoll_size];

        }

        void add_to_epoll(base_net_obj * p_obj);

        void del_from_epoll(base_net_obj * p_obj);

        void mod_from_epoll(base_net_obj * p_obj);

        int epoll_wait(std::map<ObjId, shared_ptr<base_net_obj> > &expect_list, std::map<ObjId, shared_ptr<base_net_obj> > &remove_list, uint32_t num);

    private:
        int _epoll_fd;
        struct epoll_event *_epoll_events;
        uint32_t _epoll_size;
        int _epoll_wait_time;
};

class common_obj_container
{
    public:
        common_obj_container(uint32_t thread_index, uint32_t epoll_size=DAFAULT_EPOLL_SIZE);

        ~common_obj_container();

        bool push_real_net(shared_ptr<base_net_obj> & p_obj);
        bool remove_real_net(shared_ptr<base_net_obj> & p_obj);

        shared_ptr<base_net_obj> find(uint32_t obj_id);

        bool insert(shared_ptr<base_net_obj> & p_obj);
        void erase(uint32_t obj_id);

        void handle_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg);

        void obj_process();

        common_epoll *get_epoll();

        base_timer * get_timer();

        void add_timer(shared_ptr<timer_msg> & t_msg);

        void handle_timeout(shared_ptr<timer_msg> & t_msg);

        uint32_t get_thread_index();

        uint32_t size();

    protected:
        const ObjId & gen_id_str();

    protected:
        std::map<uint32_t, shared_ptr<base_net_obj> > _obj_map;
        std::map<uint32_t, shared_ptr<base_net_obj> > _obj_net_map;

        common_epoll *_p_epoll;
        base_timer * _timer;
        ObjId _id_str;
};

class base_net_thread
{
    public:
        base_net_thread(int channel_num = 1);
        virtual ~base_net_thread();

        virtual bool start();
        void join_thread();
        virtual bool stop();
        virtual void *run();

        static void *base_thread_proc(void *arg);
        static void stop_all_thread();
        static void join_all_thread();

        bool get_run_flag();
        void set_run_flag(bool flag);
        pthread_t get_thread_id();
        uint32_t get_thread_index();

        virtual void run_process();
        virtual void net_thread_init();
        virtual void put_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg);

        static void put_obj_msg(ObjId & id, shared_ptr<normal_msg> & p_msg);

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg);

        static base_net_thread * get_base_net_thread_obj(uint32_t thread_index);

        void add_timer(shared_ptr<timer_msg> & t_msg);

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

        common_obj_container * get_net_container();

    protected:

        uint32_t _thread_index;
        pthread_t _thread_id;
        bool _run_flag;

        static std::vector<base_net_thread*> _thread_vec;
        static uint32_t _thread_index_start;
        static CRxThreadMutex _mutex;

        int _channel_num;
        std::vector< shared_ptr<base_connect<channel_data_process> > > _channel_msg_vec;

        common_obj_container * _base_container;

        static std::map<uint32_t, base_net_thread *> _base_net_thread_map;
};

class listen_process
{
    public:
        listen_process(shared_ptr<base_net_obj> p)
        {
            _listen_thread = NULL;
            _p_connect = p;
        }

        ~listen_process(){}

        void process(int fd)
        {
            shared_ptr<content_msg>  net_obj(new content_msg);
            net_obj->fd = fd;

            ObjId id;
            id._id = OBJ_ID_THREAD;
            if (_worker_thd_vec.size()) {
                int index = (unsigned long) net_obj.get() % _worker_thd_vec.size();
                id._thread_index = _worker_thd_vec[index];
            } else {
                id._thread_index = _listen_thread->get_thread_index();
            }
            shared_ptr<normal_msg> ng = static_pointer_cast<normal_msg>(net_obj);
            base_net_thread::put_obj_msg(id, ng);
        }

        void add_worker_thread(uint32_t thread_index)
        {
            _worker_thd_vec.push_back(thread_index);
        }

        void set_listen_thread(base_net_thread * thread)
        {
            _listen_thread = thread;
        }

        base_net_thread * get_listen_thread()
        {
            return _listen_thread;
        }

    protected:
        weak_ptr<base_net_obj> _p_connect;
        base_net_thread * _listen_thread;
        std::vector<uint32_t> _worker_thd_vec;
};

template<class PROCESS>
class listen_connect:public base_net_obj
{
    public:
        listen_connect(const std::string &ip, unsigned short port)
        {
            _process = NULL;

            _ip = ip;
            _port = port;

            struct sockaddr_in address;
            int reuse_addr = 1;

            memset((char *) &address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            int ret = 0;

            if (ip != "")
            {
                inet_aton(ip.c_str(), &address.sin_addr);
            }
            else
            {
                address.sin_addr.s_addr = htonl(INADDR_ANY);
            }

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
            {
                THROW_COMMON_EXCEPT("socket error " << strError(errno).c_str());
            }
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)(&(reuse_addr)), sizeof(reuse_addr));

            if (::bind(fd, (struct sockaddr *) &address, sizeof(address)) < 0)
            {
                THROW_COMMON_EXCEPT("bind error "  << strError(errno).c_str() << " " << ip << ":" << port);
            }

            ret = listen(fd, 250);
            if (ret == -1)
            {
                THROW_COMMON_EXCEPT("listen error "  << strError(errno).c_str());
            }

            set_unblock(fd);
            _fd  = fd;
        }

        virtual ~listen_connect()
        {
            if (_process)
                delete _process;
        }

        virtual void event_process(int events)
        {
            if ((events & EPOLLIN) == EPOLLIN)
            {
                int tmp_sock = 0;
                sockaddr_in addr;
                socklen_t len = 0;
                while(1)
                {
                    tmp_sock = accept(_fd, (sockaddr*)&addr, &len);
                    if (tmp_sock != -1)
                    {
                        LOG_DEBUG("recv fd[%d]\n", tmp_sock);
                        _process->process(tmp_sock);
                    }
                    else
                    {
                        LOG_WARNING("accept fail:%s", strError(errno).c_str());
                        break;
                    }
                }
            }
            else
            {
            }
        }

        virtual int real_net_process()
        {
            return 0;
        }

        void set_process(PROCESS *p)
        {
            if (_process != NULL)
                delete _process;
            _process = p;

        }

    private:
        std::string _ip;
        unsigned short _port;
        PROCESS *_process;
};

class listen_thread: public base_net_thread
{
    public:
        listen_thread():_listen_connect()
    {
    }

        virtual ~listen_thread()
        {
        }

        void init(const std::string &ip, unsigned short port)
        {
            _ip = ip;
            _port = port;
            shared_ptr<listen_connect<listen_process> > p(new listen_connect<listen_process>(ip, port));
            _listen_connect = p;
            _process =  new listen_process(_listen_connect);
            _process->set_listen_thread(this);
            _listen_connect->set_process(_process);
            _listen_connect->set_net_container(_base_container);
        }

        int add_worker_thread(uint32_t thread_index)
        {
            _process->add_worker_thread(thread_index);

            return 0;
        }

    protected:

        std::string _ip;
        unsigned short _port;
        shared_ptr<listen_connect<listen_process> > _listen_connect;
        listen_process * _process;
};

class http_base_process;

class http_base_data_process: public base_data_process
{
    public:

        http_base_data_process(http_base_process * _p_process);

        virtual ~http_base_data_process();

        virtual std::string *get_send_body(int &result);

        virtual void header_recv_finish();

        virtual void msg_recv_finish();

        virtual std::string * get_send_head();

        virtual size_t process_recv_body(const char *buf, size_t len, int& result);

        bool async_response_pending() const { return _async_response_pending; }

    protected:
        void set_async_response_pending(bool pending);
        http_base_process * _base_process;
        bool _async_response_pending;
};

class http_base_process: public base_data_process
{
    public:
        http_base_process(shared_ptr<base_net_obj> p);

        virtual ~http_base_process();

        void set_process(http_base_data_process * data_process);

        virtual size_t process_recv_buf(const char *buf, size_t len);

        virtual std::string* get_send_buf();

        virtual void handle_msg(shared_ptr<normal_msg> & p_msg);

        virtual void handle_timeout(shared_ptr<timer_msg> & t_msg);

        virtual void destroy();

        virtual void peer_close();

        http_req_head_para &get_req_head_para();

        http_res_head_para &get_res_head_para();

        void change_http_status(HTTP_STATUS status, bool if_change_send = true);
        void notify_send_ready();

        http_base_data_process *get_process();

        virtual void reset();

    protected:
        virtual size_t process_recv_body(const char *buf, size_t len, int &result) = 0;

        virtual void parse_header(std::string & recv_head) = 0;

        virtual void parse_first_line(const std::string & line) = 0;

        virtual void recv_finish() = 0;
        virtual void send_finish() = 0;

        void check_head_finish(std::string & recv_head, std::string &left_str);

        HTTP_STATUS _http_status;
        http_base_data_process *_data_process;

        http_req_head_para _req_head_para;
        http_res_head_para _res_head_para;

};

class http_res_process:public http_base_process
{
    public:
        http_res_process(shared_ptr<base_net_obj> p);

        virtual ~http_res_process();

        virtual void reset();

    protected:
        virtual size_t process_recv_body(const char *buf, size_t len, int &result);

        virtual void parse_first_line(const std::string & line);

        virtual void parse_header(std::string & recv_head);

        virtual void recv_finish();

        virtual void send_finish();

        size_t get_boundary(const char *buf, size_t len, int &result);

    protected:
        enum BOUNDARY_STATUS
        {
            BOUNDARY_RECV_HEAD = 0,
            BOUNDARY_RECV_BODY = 1,
            BOUNDARY_RECV_TAIL = 2
        };
        std::string _recv_boundary_head;
        boundary_para _boundary_para;
        BOUNDARY_STATUS _recv_boundary_status;
        uint32_t _recv_body_length;
};

#endif

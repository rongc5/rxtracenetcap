#include "legacy_core.h"

base_data_process::base_data_process(shared_ptr<base_net_obj> p)
{
    _p_connect = p;
    LOG_DEBUG("%p", this);
}

base_data_process::~base_data_process()
{
    LOG_DEBUG("%p", this);
    clear_send_list();
}

void base_data_process::peer_close()
{
    LOG_DEBUG("%p", this);
}

std::string * base_data_process::get_send_buf()
{
    LOG_DEBUG("%p", this);
    if (_send_list.begin() == _send_list.end()) {
        LOG_DEBUG("_send_list is empty");
        return NULL;
    }

    std::string *p = *(_send_list.begin());
    _send_list.erase(_send_list.begin());

    return p;
}

void base_data_process::reset()
{
    clear_send_list();
    LOG_DEBUG("%p", this);
}

size_t base_data_process::process_recv_buf(const char *buf, size_t len)
{
    (void)buf;
    LOG_DEBUG("%p", this);

    return len;
}

void base_data_process::handle_msg(shared_ptr<normal_msg> & p_msg)
{
    (void)p_msg;
    LOG_DEBUG("%p", this);
}

void base_data_process::clear_send_list()
{
    for (std::list<std::string*>::iterator itr = _send_list.begin(); itr != _send_list.end(); ++itr)
    {
        delete *itr;
    }

    _send_list.clear();
}

void base_data_process::put_send_buf(std::string * str)
{
    _send_list.push_back(str);
    shared_ptr<base_net_obj> sp = _p_connect.lock();
    if (sp)
    {
        sp->notice_send();
    }
}

shared_ptr<base_net_obj>  base_data_process::get_base_net()
{
    return _p_connect.lock();
}

void base_data_process::destroy()
{
    LOG_DEBUG("%p", this);
}

void base_data_process::add_timer(shared_ptr<timer_msg> & t_msg)
{
    shared_ptr<base_net_obj> sp = _p_connect.lock();
    if (sp)
        sp->add_timer(t_msg);
}

void base_data_process::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    (void)t_msg;
    LOG_DEBUG("%p", this);
}

base_net_obj::base_net_obj()
{
    _fd = 0;
    _epoll_event = EPOLLIN | EPOLLERR | EPOLLHUP;
    _p_net_container = NULL;
    _real_net = false;
}

base_net_obj::~base_net_obj()
{

    common_epoll * p_epoll = _p_net_container->get_epoll();
    if (p_epoll) {
        p_epoll->del_from_epoll(this);
    }

    _epoll_event = 0;

    if (_fd != 0) {
        ::close(_fd);
        LOG_DEBUG("close %d", _fd);
        _fd = 0;
    }
}

void base_net_obj::set_net_container(common_obj_container *p_net_container)
{
    _p_net_container = p_net_container;
    common_epoll * p_epoll = _p_net_container->get_epoll();

    shared_ptr<base_net_obj> p=dynamic_pointer_cast<base_net_obj>(shared_from_this());

    try {

        if (_fd > 0) {
            p_epoll->add_to_epoll(p.get());
        }
        _p_net_container->insert(p);
        add_timer();
    }
    catch (std::exception &e)
    {
        LOG_WARNING("%s", e.what());
    }
    catch (...)
    {
    }
}

bool base_net_obj::get_real_net()
{
    return _real_net;
}

void base_net_obj::set_real_net(bool real_net)
{

    _real_net = real_net;
    if (_real_net) {
    shared_ptr<base_net_obj> p=dynamic_pointer_cast<base_net_obj>(shared_from_this());

        LOG_DEBUG(".use_count:%d, _id:%d, _thread_index:%d", p.use_count(), p->get_id()._id, p->get_id()._thread_index);
        _p_net_container->push_real_net(p);

        LOG_DEBUG(".use_count:%d, _id:%d, _thread_index:%d", p.use_count(), p->get_id()._id, p->get_id()._thread_index);
    }
}

common_obj_container * base_net_obj::get_net_container()
{
    return _p_net_container;
}

void base_net_obj::update_event(int event)
{
    if (!_p_net_container)
    {
        return;
    }

    common_epoll * p_epoll = _p_net_container->get_epoll();
    if (_epoll_event != event && p_epoll) {
        _epoll_event = event;
        p_epoll->mod_from_epoll(this);
    }
}

int base_net_obj::get_event()
{
    return _epoll_event;
}

int base_net_obj::get_sfd()
{
    return _fd;
}

void base_net_obj::set_id(const ObjId & id_str)
{
    _id_str = id_str;
}

const ObjId & base_net_obj::get_id()
{
    return _id_str;
}

void base_net_obj::handle_msg(shared_ptr<normal_msg> & p_msg)
{
    (void)p_msg;
}

void base_net_obj::notice_send()
{
}

void base_net_obj::destroy()
{
}

void base_net_obj::add_timer(shared_ptr<timer_msg> & t_msg)
{
    if (_p_net_container)
    {
        _p_net_container->add_timer(t_msg);
        add_timer();
    }
    else
    {
        _timer_vec.push_back(t_msg);
    }
}

void base_net_obj::add_timer()
{
    std::vector<shared_ptr<timer_msg> >::iterator it;

    bool flag = false;
    for (it = _timer_vec.begin(); it != _timer_vec.end(); it++)
    {
        if (_p_net_container)
        {
            _p_net_container->add_timer(*it);
            flag = true;
        }
        else
        {
            break;
        }
    }

    if (flag)
    {
        _timer_vec.clear();
    }
}

net_addr & base_net_obj::get_peer_addr()
{
    if (!_peer_net.ip.empty() && _peer_net.port)
    {
        return _peer_net;
    }

    struct sockaddr_in sa;
    socklen_t len = sizeof(struct sockaddr_in);

    if (!getpeername(_fd, (struct sockaddr *)&sa, &len))
    {
        _peer_net.ip = inet_ntoa(sa.sin_addr);
        _peer_net.port = ntohs(sa.sin_port);
    }

    return _peer_net;
}

void base_net_obj::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    (void)t_msg;
}

base_timer::base_timer(common_obj_container * net_container)
{
    _net_container = net_container;
    _timerid = TIMER_ID_BEGIN;
    _current = 0;
}

base_timer::~base_timer()
{
    _timer_list[_current].clear();
    _timer_list[1 - _current].clear();
}

uint32_t base_timer::gen_timerid()
{
    do
    {
        _timerid++;
        if (_timerid < TIMER_ID_BEGIN)
            _timerid = TIMER_ID_BEGIN;

    }while (_timerid_set.count(_timerid));

    _timerid_set.insert(_timerid);

    return _timerid;
}

uint32_t base_timer::add_timer(shared_ptr<timer_msg> & t_msg)
{
    if (t_msg->_time_length <= 0 || t_msg->_obj_id <= 0)
    {
        LOG_WARNING("add_timer failed: time_length:%u timer_id:%u _timer_type:%u",
                t_msg->_time_length, t_msg->_timer_id, t_msg->_timer_type);
        return 0;
    }

    uint64_t reach_time = GetMilliSecond() + t_msg->_time_length;
    t_msg->_timer_id = gen_timerid();

    _timer_list[_current].insert(std::make_pair(reach_time, t_msg));

    return t_msg->_timer_id;
}

void base_timer::check_timer(std::vector<uint32_t> &expect_list)
{
    uint64_t now =     GetMilliSecond();

    typedef std::multimap<uint64_t, shared_ptr<timer_msg> >::iterator MITER;
    MITER it;
    std::pair<MITER, MITER> range;

    std::multimap<uint64_t, shared_ptr<timer_msg> > & timer_list = _timer_list[_current];
    it = timer_list.begin();

    std::vector<uint64_t> tmp_vec;
    std::vector<uint64_t>::iterator ii;

    _current = 1 - _current;

    for (it = timer_list.begin(); it != timer_list.end(); it++)
    {
        try
        {
            if (it->first > now)
                break;

            tmp_vec.push_back(it->first);

            _net_container->handle_timeout(it->second);
        }
        catch(CMyCommonException &e)
        {
            expect_list.push_back(it->second->_obj_id);
        }
        catch(std::exception &e)
        {
            expect_list.push_back(it->second->_obj_id);
        }
    }

    for (ii = tmp_vec.begin(); ii != tmp_vec.end(); ii++)
    {

        range = timer_list.equal_range(*ii);
        for (it = range.first; it != range.second; ++it)
        {
            _timerid_set.erase(it->second->_timer_id);
        }
        timer_list.erase(*ii);
    }

}

bool base_timer::is_empty()
{
    std::multimap<uint64_t, shared_ptr<timer_msg> > & timer_list = _timer_list[_current];
    return timer_list.begin() == timer_list.end();
}

channel_data_process::channel_data_process(shared_ptr<base_net_obj> p, int channelid)
    : base_data_process(p), _current(0), _channelid(channelid)
{
    _last_time = 0;
}

size_t channel_data_process::process_recv_buf(const char *buf, size_t len)
{
    std::deque<normal_obj_msg> processing_queue;

    {
        CRxThreadLock lck(&_mutex);

        if (_queue[_current].empty()) {
            _current = 1 - _current;
        }

        processing_queue.swap(_queue[_current]);
    }

    LOG_DEBUG("buf:%s, len:%d, processing_queue.size:%d", buf, len, processing_queue.size());

    size_t i = 0;
    shared_ptr<base_net_obj> sp = _p_connect.lock();
    for (std::deque<normal_obj_msg>::iterator it = processing_queue.begin(); it != processing_queue.end(); ++it, ++i)
    {
        if (sp)
        {
            sp->get_net_container()->handle_msg(it->_id, it->p_msg);
        }
    }

    size_t k = i * sizeof(CHANNEL_MSG_TAG);

    if (!_last_time)
        add_event_timer();

    _last_time = GetMilliSecond();

    return k;
}

void channel_data_process::put_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg)
{
    CRxThreadLock lck(&_mutex);

    normal_obj_msg obj_msg;
    obj_msg.p_msg = p_msg;
    obj_msg._id = obj_id;

    int idle = 1 - _current;
    _queue[idle].push_back(obj_msg);

    send(_channelid, CHANNEL_MSG_TAG, sizeof(CHANNEL_MSG_TAG), MSG_DONTWAIT);

    return;
}

void channel_data_process::add_event_timer(uint64_t time_out)
{
    time_out = time_out > 100 * MAX_CHANNEL_EVENT_TIMEOUT ? 100 * MAX_CHANNEL_EVENT_TIMEOUT : time_out;
    time_out = time_out < 20 ? 20 : time_out;

    shared_ptr<timer_msg> t_msg(new timer_msg);
    t_msg->_obj_id = OBJ_ID_DOMAIN;
    t_msg->_timer_type = NONE_CHANNEL_EVENT_TIMER_TYPE;
    t_msg->_time_length = time_out;

    add_timer(t_msg);
}

void channel_data_process::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    if (!t_msg.get())
        return;

    int len = 0;
    {
        CRxThreadLock lck(&_mutex);
        len = _queue[0].size() + _queue[1].size();
    }

    LOG_DEBUG("handle_timeout: timer_id:%u timer_type:%u, len:%d", t_msg->_timer_id, t_msg->_timer_type, len);

    uint64_t now = GetMilliSecond();
    if (t_msg->_timer_type == NONE_CHANNEL_EVENT_TIMER_TYPE)
    {
        if (llabs(now - _last_time) > t_msg->_time_length && len)
        {
            get_base_net()->set_real_net(true);
            add_event_timer(t_msg->_time_length / 2);
        }
        else
        {
            add_event_timer(t_msg->_time_length * 2);
        }
    }
}

void common_epoll::add_to_epoll(base_net_obj * p_obj)
{
    int tmpOprate = EPOLL_CTL_ADD;
    struct epoll_event tmpEvent;
    memset(&tmpEvent, 0, sizeof(epoll_event));
    tmpEvent.events = p_obj->get_event();
    tmpEvent.data.ptr = p_obj;
    int ret = epoll_ctl(_epoll_fd, tmpOprate, p_obj->get_sfd(), &tmpEvent);
    LOG_DEBUG("add to epoll _epoll_fd[%d] _get_sock [%d]", _epoll_fd, p_obj->get_sfd());
    if (ret != 0) {
        int e = errno;
        if (e == EEXIST) {

            tmpOprate = EPOLL_CTL_MOD;
            int r2 = epoll_ctl(_epoll_fd, tmpOprate, p_obj->get_sfd(), &tmpEvent);
            if (r2 == 0) return;
            std::string err2 = strError(errno);
            LOG_DEBUG("epoll MOD after EEXIST failed: %s", err2.c_str());
        }
        std::string err = strError(e);
        LOG_DEBUG("add to epoll fail %s", err.c_str());
        THROW_COMMON_EXCEPT("add to epoll fail " << err);
    }
}

void common_epoll::del_from_epoll(base_net_obj * p_obj)
{
    int tmpOprate = EPOLL_CTL_DEL;
    struct epoll_event tmpEvent;
    memset(&tmpEvent, 0, sizeof(epoll_event));
    tmpEvent.data.ptr = p_obj;
    int ret = epoll_ctl(_epoll_fd, tmpOprate, p_obj->get_sfd(), &tmpEvent);
    LOG_DEBUG("delete to epoll _epoll_fd[%d] _get_sock [%d]", _epoll_fd, p_obj->get_sfd());
    if (ret != 0) {
        int err = errno;
        if (err == EBADF || err == ENOENT) {

            LOG_TRACE_MSG("del_from_epoll benign err fd[%d]: %s", p_obj->get_sfd(), strError(err).c_str());
            return;
        }
        THROW_COMMON_EXCEPT("del from epoll fail " << strError(err).c_str());
    }
}

void common_epoll::mod_from_epoll(base_net_obj * p_obj)
{
    int tmpOprate = EPOLL_CTL_MOD;
    struct epoll_event tmpEvent;
    tmpEvent.events =  p_obj->get_event();
    tmpEvent.data.ptr = p_obj;
    int ret = epoll_ctl(_epoll_fd, tmpOprate, p_obj->get_sfd(), &tmpEvent);
    if (ret != 0) {
        if (errno == ENOENT) {

            tmpOprate = EPOLL_CTL_ADD;
            int r2 = epoll_ctl(_epoll_fd, tmpOprate, p_obj->get_sfd(), &tmpEvent);
            if (r2 == 0) return;
        }
        THROW_COMMON_EXCEPT("mod from epoll fail "<< strError(errno).c_str());
    }
}

int common_epoll::epoll_wait(std::map<ObjId, shared_ptr<base_net_obj> > &expect_list, std::map<ObjId, shared_ptr<base_net_obj> > &remove_list, uint32_t num)
{
    (void)num;
    int  nfds = ::epoll_wait(_epoll_fd, _epoll_events, _epoll_size, _epoll_wait_time);
    if (nfds == -1)
    {
        std::string err = strError(errno);
        LOG_DEBUG("epoll_wait fail [%s]", err.c_str());
        if (errno == EINTR)
            return 0;
        THROW_COMMON_EXCEPT("epoll_wait fail "<< err);

    }

    for (int i =0; i < nfds; i++)
    {

        if (_epoll_events[i].data.ptr != NULL)
        {
            base_net_obj * p = (base_net_obj*)(_epoll_events[i].data.ptr);
            if (p != NULL)
            {
                shared_ptr<base_net_obj> p_obj=dynamic_pointer_cast<base_net_obj>(p->shared_from_this());
                try
                {

                    p->event_process(_epoll_events[i].events);
                    if (p->get_real_net()) {
                        remove_list.insert(std::make_pair(p->get_id(), p_obj));
                    }
                }
                catch(CMyCommonException &e)
                {
                    expect_list.insert(std::make_pair(p->get_id(), p_obj));
                }
                catch(std::exception &e)
                {
                    expect_list.insert(std::make_pair(p->get_id(), p_obj));
                }
            }
        }
    }
    return nfds;
}

common_obj_container::common_obj_container(uint32_t thread_index, uint32_t epoll_size)
{
    _p_epoll = new common_epoll();
    _p_epoll->init(epoll_size);

    _timer = new base_timer(this);

    _id_str._id = OBJ_ID_BEGIN;
    _id_str._thread_index = thread_index;
}

common_obj_container::~common_obj_container()
{
    std::map<uint32_t, shared_ptr<base_net_obj> >::iterator it;
    for (it = _obj_map.begin(); it != _obj_map.end(); ++it)
    {
        it->second->destroy();
    }

    if (_p_epoll != NULL)
        delete _p_epoll;

    if (_timer)
        delete _timer;
}

common_epoll * common_obj_container::get_epoll()
{
        return _p_epoll;
}

base_timer * common_obj_container::get_timer()
{
        return _timer;
}

const ObjId & common_obj_container::gen_id_str()
{
    do {
        if (_id_str._id <= OBJ_ID_BEGIN)
            _id_str._id = OBJ_ID_BEGIN;

        _id_str._id++;
    } while (find(_id_str._id));

    return _id_str;
}

bool common_obj_container::push_real_net(shared_ptr<base_net_obj> & p_obj)
{
    LOG_DEBUG("base_net_obj:%d, .use_count:%d, _id:%d _thread_index:%d", p_obj.get(), p_obj.use_count(), p_obj->get_id()._id, p_obj->get_id()._thread_index);

    std::map<uint32_t, shared_ptr<base_net_obj> >::iterator it = _obj_net_map.find(p_obj->get_id()._id);
    if (it == _obj_net_map.end())
    {
        _obj_net_map.insert(std::make_pair(p_obj->get_id()._id, p_obj));
    }

    LOG_DEBUG("base_net_obj:%d, .use_count:%d, _id:%d _thread_index:%d", p_obj.get(), p_obj.use_count(), p_obj->get_id()._id, p_obj->get_id()._thread_index);

    return true;
}

bool common_obj_container::remove_real_net(shared_ptr<base_net_obj> & p_obj)
{
    LOG_DEBUG("base_net_obj:%d, .use_count:%d, _id:%d _thread_index:%d", p_obj.get(), p_obj.use_count(), p_obj->get_id()._id, p_obj->get_id()._thread_index);
    _obj_net_map.erase(p_obj->get_id()._id);
    LOG_DEBUG("base_net_obj:%d, .use_count:%d, _id:%d _thread_index:%d", p_obj.get(), p_obj.use_count(), p_obj->get_id()._id, p_obj->get_id()._thread_index);

    return true;
}

uint32_t common_obj_container::size()
{
    return _obj_map.size();
}

bool common_obj_container::insert(shared_ptr<base_net_obj> &p_obj)
{
    p_obj->set_id(gen_id_str());

    _obj_map[p_obj->get_id()._id] = p_obj;

    return true;
}

uint32_t common_obj_container::get_thread_index()
{
    return _id_str._thread_index;
}

void common_obj_container::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    switch (t_msg->_obj_id)
    {
        case OBJ_ID_THREAD:
            {
                base_net_thread * net_thread = base_net_thread::get_base_net_thread_obj(_id_str._thread_index);
                if (net_thread)
                {
                    net_thread->handle_timeout(t_msg);
                }
            }
            break;
        default:
            {
                shared_ptr<base_net_obj>  net_obj = find(t_msg->_obj_id);
                if (net_obj)
                    net_obj->handle_timeout(t_msg);
            }
    }

}

void common_obj_container::add_timer(shared_ptr<timer_msg> & t_msg)
{
    if (_timer)
    {
        _timer->add_timer(t_msg);
    }
}

shared_ptr<base_net_obj> common_obj_container::find(uint32_t obj_id)
{
    std::map<uint32_t, shared_ptr<base_net_obj> >::iterator it = _obj_map.find(obj_id);
    if (it == _obj_map.end()){
        return shared_ptr<base_net_obj>();
    }else {
        return it->second;
    }
}

void common_obj_container::erase(uint32_t obj_id)
{
    _obj_net_map.erase(obj_id);
    _obj_map.erase(obj_id);

    return;
}

void common_obj_container::handle_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg)
{
    switch (obj_id)
    {
        case OBJ_ID_THREAD:
            {
                base_net_thread * net_thread = base_net_thread::get_base_net_thread_obj(_id_str._thread_index);
                if (net_thread)
                {
                    net_thread->handle_msg(p_msg);
                }
            }
            break;
        default:
            {

                shared_ptr<base_net_obj>  net_obj = find(obj_id);
                if (net_obj)
                    net_obj->handle_msg(p_msg);
            }
    }
}

void common_obj_container::obj_process()
{
    uint32_t tmp_num = 0;

    std::vector<shared_ptr<base_net_obj> > exception_vec;
    std::vector<shared_ptr<base_net_obj> > real_net_vec;

    std::map<uint32_t, shared_ptr<base_net_obj> >::iterator it;
    for (it = _obj_net_map.begin(); it != _obj_net_map.end(); ++it)
    {
        try
        {
            it->second->real_net_process();
            if (!it->second->get_real_net()) {
                real_net_vec.push_back(it->second);
            }

            tmp_num++;
        }
        catch(CMyCommonException &e)
        {
            exception_vec.push_back(it->second);
        }
        catch(std::exception &e)
        {
            exception_vec.push_back(it->second);
        }
    }

    for (std::vector<shared_ptr<base_net_obj> >::iterator tmp_itr = real_net_vec.begin();
            tmp_itr != real_net_vec.end(); tmp_itr++) {
        LOG_DEBUG("remove_real_net: _id:%d, _thread_index:%d", (*tmp_itr)->get_id()._id, (*tmp_itr)->get_id()._thread_index);
        remove_real_net(*tmp_itr);
    }

    for (std::vector<shared_ptr<base_net_obj> >::iterator tmp_itr = exception_vec.begin();
            tmp_itr != exception_vec.end(); tmp_itr++) {
        (*tmp_itr)->destroy();
        erase((*tmp_itr)->get_id()._id);
    }

    std::map<ObjId, shared_ptr<base_net_obj> > exp_list;
    std::map<ObjId, shared_ptr<base_net_obj> > remove_list;

    _p_epoll->epoll_wait(exp_list, remove_list, tmp_num);
    for (std::map<ObjId, shared_ptr<base_net_obj> >::iterator itr = exp_list.begin(); itr != exp_list.end(); ++itr)
    {
        LOG_DEBUG("step2: _id:%d, _thread_index:%d", itr->second->get_id()._id, itr->second->get_id()._thread_index);
        itr->second->destroy();
        erase(itr->first._id);
    }

    for (std::map<ObjId, shared_ptr<base_net_obj> >::iterator itr = remove_list.begin(); itr != remove_list.end(); ++itr)
    {
        LOG_DEBUG("remove_real_net: _id:%d, _thread_index:%d", itr->second->get_id()._id, itr->second->get_id()._thread_index);
        itr->second->set_real_net(false);
        remove_real_net(itr->second);
    }

    std::vector<uint32_t> exp_vec;

    get_timer()->check_timer(exp_vec);

    for (std::vector<uint32_t>::iterator it = exp_vec.begin(); it != exp_vec.end(); it++)
    {
        shared_ptr<base_net_obj>  net_obj = find(*it);
        if (net_obj)
        {
            net_obj->destroy();
            erase(*it);
        }
    }
}

std::vector<base_net_thread*> base_net_thread::_thread_vec;
uint32_t base_net_thread::_thread_index_start = 0;
CRxThreadMutex base_net_thread::_mutex;
std::map<uint32_t, base_net_thread *> base_net_thread::_base_net_thread_map;

base_net_thread::base_net_thread(int channel_num)
    : _thread_id(0), _run_flag(true), _channel_num(channel_num), _base_container(NULL)
{
    CRxThreadLock lck(&_mutex);
    _thread_index_start++;
    _thread_index = _thread_index_start;

    net_thread_init();
}

base_net_thread::~base_net_thread(){
    if (_base_container){
        delete _base_container;
    }
}

bool base_net_thread::start()
{
    if (_thread_id) {
        return false;
    }

    int ret = pthread_create(&_thread_id, NULL, base_thread_proc, this);
    if (ret != 0)
    {
        return false;
    }
    _thread_vec.push_back(this);
    return true;
}

void base_net_thread::join_thread()
{
    pthread_join(_thread_id, NULL);
}

bool base_net_thread::stop()
{
    _run_flag = false;
    return true;
}

void * base_net_thread::base_thread_proc(void *arg)
{
    base_net_thread *p = (base_net_thread*)arg;
    return p->run();
}

void base_net_thread::stop_all_thread()
{
    std::vector<base_net_thread*>::iterator it;
    for (it = _thread_vec.begin(); it != _thread_vec.end(); it++){
        (*it)->stop();
    }
}

void base_net_thread::join_all_thread()
{
    std::vector<base_net_thread*>::iterator it;
    for (it = _thread_vec.begin(); it != _thread_vec.end(); it++){
        (*it)->join_thread();
    }
}

bool base_net_thread::get_run_flag()
{
    return _run_flag;
}

void base_net_thread::set_run_flag(bool flag)
{
    _run_flag = flag;
}

pthread_t base_net_thread::get_thread_id()
{
    return _thread_id;
}

uint32_t base_net_thread::get_thread_index()
{
    return _thread_index;
}

void * base_net_thread::run()
{
    while (get_run_flag()) {
        run_process();
        _base_container->obj_process();
    }

    return NULL;
}

void base_net_thread::run_process()
{

}

void base_net_thread::net_thread_init()
{
    _base_container = new common_obj_container(get_thread_index());

    for (int i = 0; i < _channel_num; i++) {

        int fd[2];
        int ret = socketpair(AF_UNIX,SOCK_STREAM,0,fd);
        if (ret < 0) {
            return ;
        }

        shared_ptr<base_connect<channel_data_process> >  channel_connect(new base_connect<channel_data_process>(fd[0]));
        channel_data_process * data_process = new channel_data_process(channel_connect, fd[1]);
        channel_connect->set_process(data_process);
        channel_connect->set_net_container(_base_container);

        _channel_msg_vec.push_back(channel_connect);
    }

    _base_net_thread_map[get_thread_index()] = this;
}

void base_net_thread::put_msg(uint32_t obj_id, shared_ptr<normal_msg> & p_msg)
{
    int index = (unsigned long) (&p_msg) % _channel_msg_vec.size();
    shared_ptr<base_connect<channel_data_process> >  channel_connect = _channel_msg_vec[index];
    channel_data_process * data_process = channel_connect->process();
    if (data_process)
    {
        data_process->put_msg(obj_id, p_msg);

    }
}

void base_net_thread::handle_msg(shared_ptr<normal_msg> & p_msg)
{
    (void)p_msg;
}

base_net_thread * base_net_thread::get_base_net_thread_obj(uint32_t thread_index)
{
    std::map<uint32_t, base_net_thread *>::const_iterator it = _base_net_thread_map.find(thread_index);
    if (it != _base_net_thread_map.end()){
        return it->second;
    }

    return NULL;
}

void base_net_thread::add_timer(shared_ptr<timer_msg> & t_msg)
{
    _base_container->add_timer(t_msg);
}

void base_net_thread::put_obj_msg(ObjId & id, shared_ptr<normal_msg> & p_msg)
{
    base_net_thread * net_thread = get_base_net_thread_obj(id._thread_index);
    if (!net_thread) {
        return;
    }

    net_thread->put_msg(id._id, p_msg);
}

void base_net_thread::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    (void)t_msg;
}

common_obj_container * base_net_thread::get_net_container()
{
    return _base_container;
}

http_base_data_process::http_base_data_process(http_base_process * _p_process):
    base_data_process(_p_process->get_base_net())
{
    LOG_DEBUG("%p", this);
    _base_process = _p_process;
    _async_response_pending = false;
}

http_base_data_process::~http_base_data_process()
{
}

std::string *http_base_data_process::get_send_body(int &result)
{
    (void)result;
    LOG_DEBUG("%p", this);

    return NULL;
}

void http_base_data_process::header_recv_finish()
{
    LOG_DEBUG("%p", this);
}

void http_base_data_process::msg_recv_finish()
{
    LOG_DEBUG("%p", this);
}

std::string *http_base_data_process::get_send_head()
{
    LOG_DEBUG("%p", this);

    return NULL;
}

size_t http_base_data_process::process_recv_body(const char *buf, size_t len, int& result)
{
    (void)buf;
    LOG_DEBUG("%p", this);

    result = 1;

    return len;
}

void http_base_data_process::set_async_response_pending(bool pending)
{
    _async_response_pending = pending;
}

http_base_process::http_base_process(shared_ptr<base_net_obj> p):base_data_process(p)
{
    _data_process = NULL;
}

http_base_process::~http_base_process()
{
    if (_data_process != NULL)
        delete _data_process;
}

http_req_head_para &http_base_process::get_req_head_para()
{
    return _req_head_para;
}

http_res_head_para &http_base_process::get_res_head_para()
{
    return _res_head_para;
}

void http_base_process::reset()
{
    _req_head_para.init();
    _res_head_para.init();
}

void http_base_process::set_process(http_base_data_process * data_process)
{
    if (_data_process != NULL && _data_process != data_process) {
        delete _data_process;
        _data_process = NULL;
    }
    _data_process = data_process;
}

size_t http_base_process::process_recv_buf(const char *buf, size_t len)
{
    if (_http_status > RECV_BODY)
    {
        THROW_COMMON_EXCEPT("http recv status not correct " << _http_status);
    }

    size_t ret = 0;
    bool staus_change = false;
    std::string left_str, recv_head;
    if (_http_status == RECV_HEAD && strstr(buf, CRLF2))
    {
        recv_head.append(buf, len);
        check_head_finish(recv_head, left_str);
        staus_change = true;
    }

    if (_http_status == RECV_BODY)
    {
        int result = 0;
        if (staus_change)
        {
            ret = len - left_str.length();
            ret += process_recv_body((char*)left_str.c_str(), left_str.length(), result);
        }
        else
            ret = process_recv_body(buf, len, result);

        if (result == 1)
        {
            recv_finish();
        }
    }
    return ret;
}

std::string* http_base_process::get_send_buf()
{
    if (_http_status < SEND_HEAD)
    {
        return NULL;
    }

    std::string *ret_str = NULL;
    if (_http_status == SEND_HEAD)
    {
        ret_str = _data_process->get_send_head();

        if (!ret_str)
            return NULL;

        _http_status = SEND_BODY;
        return ret_str;
    }
    else if (_http_status == SEND_BODY)
    {
        int result = 0;
        ret_str = _data_process->get_send_body(result);
        if (result == 1)
            send_finish();
    }

    return ret_str;
}

void http_base_process::handle_msg(shared_ptr<normal_msg> & p_msg)
{
    _data_process->handle_msg(p_msg);
    return;
}

void http_base_process::handle_timeout(shared_ptr<timer_msg> & t_msg)
{
    _data_process->handle_timeout(t_msg);
}

void http_base_process::destroy()
{
    _data_process->destroy();
}

void http_base_process::peer_close()
{
    _data_process->peer_close();
}

void http_base_process::change_http_status(HTTP_STATUS status, bool if_change_send)
{
    shared_ptr<base_net_obj> sp = _p_connect.lock();
    _http_status = status;
    if (status == SEND_HEAD && if_change_send && sp)
    {
        sp->notice_send();
    }
}

void http_base_process::notify_send_ready()
{
    change_http_status(SEND_HEAD);
}

http_base_data_process * http_base_process::get_process()
{
    return _data_process;
}

void http_base_process::check_head_finish(std::string & recv_head, std::string &left_str)
{
    size_t pos =  recv_head.find(CRLF2);
    if (pos != std::string::npos)
    {
        left_str = recv_head.substr(pos + 4);
        recv_head.erase(pos + 4);

        change_http_status(RECV_BODY);
        parse_header(recv_head);

        _data_process->header_recv_finish();
    }
    else
    {
        if (recv_head.length() > MAX_HTTP_HEAD_LEN)
        {
            THROW_COMMON_EXCEPT("http head too long (" << recv_head.length() << ")");
        }
    }
}

http_res_process::http_res_process(shared_ptr<base_net_obj>  p):http_base_process(p)
{
    change_http_status(RECV_HEAD);
    _recv_body_length = 0;
    _recv_boundary_status = BOUNDARY_RECV_HEAD;
}

http_res_process::~http_res_process()
{
}

void http_res_process::reset()
{
    http_base_process::reset();
    change_http_status(RECV_HEAD);

    _boundary_para.init();
    _recv_body_length = 0;
    _recv_boundary_status = BOUNDARY_RECV_HEAD;
}

size_t http_res_process::process_recv_body(const char *buf, size_t len, int &result)
{

    int ret = 0;
    if (strcasecmp(_req_head_para._method.c_str(), "GET") == 0 || strcasecmp(_req_head_para._method.c_str(), "HEAD") == 0)
    {
        result = 1;
        ret = len;
    }
    else
    {
        if (_boundary_para._boundary_str.length() == 0)
        {
            ret = _data_process->process_recv_body(buf, len, result);
            _recv_body_length += ret;

            uint64_t content_length = 0;
            std::string *tmp_str = _req_head_para.get_header("Content-Length");
            if (tmp_str)
            {
                content_length = strtoull(tmp_str->c_str(), 0, 10);
            }

            if (_recv_body_length == content_length)
            {
                result = 1;
            }
        }
        else
        {
            ret = get_boundary(buf, len, result);
        }
    }
    return ret;
}

void http_res_process::parse_first_line(const std::string & line)
{
    std::vector<std::string> tmp_vec;
    SplitString(line.c_str(), " ", &tmp_vec, SPLIT_MODE_ALL);
    if (tmp_vec.size() != 3) {
        THROW_COMMON_EXCEPT("http first line");
    }
    _req_head_para._method = tmp_vec[0];
    _req_head_para._version = tmp_vec[2];
    _req_head_para._url_path = tmp_vec[1];
}

void http_res_process::parse_header(std::string & recv_head)
{
    std::string &head_str = recv_head;
    std::vector<std::string> strList;
    SplitString(head_str.c_str(), CRLF, &strList, SPLIT_MODE_ALL);
    for (uint32_t i = 0; i < strList.size(); i++) {
        if (!i) {
            parse_first_line(strList[i]);
        }else {
            std::vector<std::string> tmp_vec;
            SplitString(strList[i].c_str(), ":", &tmp_vec, SPLIT_MODE_ONE);
            if (2 == tmp_vec.size()) {
                _req_head_para._headers.insert(make_pair(tmp_vec[0], tmp_vec[1]));
                LOG_DEBUG("%s: %s", tmp_vec[0].c_str(), tmp_vec[1].c_str());
            }
        }
    }

    std::string * cookie_str = _req_head_para.get_header("Cookie");
    if (cookie_str)
    {
        std::vector<std::string> cookie_vec;
        SplitString(cookie_str->c_str(), ";", &cookie_vec, SPLIT_MODE_ALL);
        size_t c_num = cookie_vec.size();
        for (size_t ii = 0; ii < c_num; ii++)
        {
            std::vector<std::string> c_tmp_vec;
            SplitString(cookie_vec[ii].c_str(), "=", &c_tmp_vec, SPLIT_MODE_ONE);
            if (c_tmp_vec.size() == 2)
            {
                StringTrim(c_tmp_vec[0]);
                StringTrim(c_tmp_vec[1]);
                _req_head_para._cookie_list.insert(make_pair(c_tmp_vec[0], c_tmp_vec[1]));
            }
        }
    }

    std::string *tmp_str = NULL;
    if (_req_head_para._method == "POST" || _req_head_para._method == "PUT")
    {

        tmp_str = _req_head_para.get_header("Content-Type");
        if (tmp_str)
        {
            if (strcasestr(tmp_str->c_str(), "multipart/form-data") != NULL)
            {
                GetCaseStringByLabel(*tmp_str, "boundary=", "", _boundary_para._boundary_str);
            }
        }
    }
}

void http_res_process::recv_finish()
{
    _data_process->msg_recv_finish();
    http_base_data_process* data_process = get_process();
    if (data_process && data_process->async_response_pending()) {
        LOG_DEBUG("http_res_process: waiting for async response");
        return;
    }
    change_http_status(SEND_HEAD);
}

void http_res_process::send_finish()
{
    reset();
}

size_t http_res_process::get_boundary(const char *buf, size_t len, int &result)
{
    uint64_t content_length = 0;
    std::string *tmp_str = _req_head_para.get_header("Content-Length");
    if (tmp_str)
    {
        content_length = strtoull(tmp_str->c_str(), 0, 10);
    }

    if (!content_length)
    {
        THROW_COMMON_EXCEPT("get boundary but content_len not found");
    }
    size_t ret = len;
    size_t p_len = 0;
    result = 0;
    _recv_body_length += len;

    if (_recv_boundary_status == BOUNDARY_RECV_HEAD)
    {
        _recv_boundary_head.append(buf, len);
        size_t pos = _recv_boundary_head.find("\r\n\r\n");
        if (pos != std::string::npos)
        {
            _boundary_para._boundary_content_length = content_length - (_boundary_para._boundary_str.length() + BOUNDARY_EXTRA_LEN)
                - (pos+4);
            std::string left_str;
            if (_recv_body_length == content_length)
            {
                left_str = _recv_boundary_head.substr(pos+4, _boundary_para._boundary_content_length);
            }
            else if (_recv_body_length >= content_length - (_boundary_para._boundary_str.length() + BOUNDARY_EXTRA_LEN))
            {
                left_str = _recv_boundary_head.substr(pos+4, _boundary_para._boundary_content_length);
                _recv_boundary_status = BOUNDARY_RECV_TAIL;
            }
            else
            {
                left_str = _recv_boundary_head.substr(pos+4);
                _recv_boundary_status = BOUNDARY_RECV_BODY;
            }

            if (left_str.length() > 0)
            {
                p_len = _data_process->process_recv_body(left_str.c_str(), left_str.length(), result);
                if (_recv_body_length == content_length)
                    result = 1;
                p_len = left_str.length() - p_len;
            }
            else
            {
            }
        }
        else
        {
            if (_recv_boundary_head.length() >= MAX_HTTP_HEAD_LEN)
                THROW_COMMON_EXCEPT("http boundary head too long (" << _recv_boundary_head.length() << ")");
        }
    }
    else if (_recv_boundary_status == BOUNDARY_RECV_BODY)
    {
        int tmp_len = len;
        if (_recv_body_length == content_length)
        {
            tmp_len = len - (_boundary_para._boundary_str.length() + BOUNDARY_EXTRA_LEN);

        }
        else if (_recv_body_length >= content_length - (_boundary_para._boundary_str.length() + BOUNDARY_EXTRA_LEN))
        {
            tmp_len = len - (_recv_body_length - (content_length - (_boundary_para._boundary_str.length() + BOUNDARY_EXTRA_LEN)));
            _recv_boundary_status = BOUNDARY_RECV_TAIL;
        }
        else
        {
        }

        p_len = _data_process->process_recv_body(buf, tmp_len, result);
        p_len = tmp_len - p_len;

        if (_recv_body_length == content_length)
            result = 1;
    }
    else
    {

        if (_recv_body_length == content_length)
            result = 1;
    }
    _recv_body_length = _recv_body_length - p_len;
    return ret - p_len;
}

#pragma once

#include "common.hpp"

namespace es
{

namespace _internal
{

struct tcp_client_ctx
{
    long long          sk; // sk > 0, connecting ; sk <= 0, disconnected
    list<auto_memory>  memory_list;
    mutex              list_mtx;
};

}

namespace tcp
{

using namespace ::es::_internal;

class receiver
{
public:
    static ref<receiver> build(int port);

    ~receiver();

    bool read(long long& client_id, void* output_buf, long long& read_len);

    void close();

private:
    receiver();

    bool init(int port);

private:
    long long                  _sk;
    volatile bool              _stop;
    atom<long long>            _thread_cnt;
    list<ref<tcp_client_ctx>>  _client_list;
    rw_mtx                     _client_list_mtx;
};

class sender
{
public:
    static ref<sender> build(const char* ip, int port);

    ~sender();

    bool send(const void* buf, long long send_len);

    void close();

private:
    sender();

    bool init(const char* ip, int port);

private:
    long long _sk;
    mutex     _mtx;
};

}
}

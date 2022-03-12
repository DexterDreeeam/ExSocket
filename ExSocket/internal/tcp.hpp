#pragma once

#include "common.hpp"

namespace es
{

namespace tcp
{

class notifier
{
public:
    notifier() = default;

    virtual ~notifier() = default;

    virtual void on_client_connect(long long client) = 0;

    virtual void on_client_disconnect(long long client) = 0;

    virtual void on_message_arrive(long long client, void* msg, long long msg_len) = 0;
};

class receiver
{
public:
    static es::_internal::ref<receiver> build(int port, notifier* notifier);

    ~receiver();

    void close();

private:
    receiver();

    bool init(int port, notifier* notifier);

    void main_thread();

    void client_thread(long long client);

private:
    long long                      _sk;
    volatile bool                  _stop;
    es::_internal::atom<long long> _thread_cnt;
    notifier*                      _notifier;
};

class sender
{
public:
    static es::_internal::ref<sender> build(const char* ip, int port);

    ~sender();

    bool send(const void* buf, long long send_len);

    void close();

private:
    sender();

    bool init(const char* ip, int port);

private:
    long long              _sk;
    es::_internal::mutex   _mtx;
};

}
}

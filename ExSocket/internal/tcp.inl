#pragma once

#include "common.hpp"
#include "tcp.hpp"

namespace es
{

namespace _internal
{

/*
 *   ·--------------------------------------·
 *   |    session_id    |      data_len     |
 *   |--------------------------------------|
 *   |-                                    -|
 *   |-                DATA                -|
 *   |-                                    -|
 *   ·--------------------------------------·
 */
struct tcp_packet
{
    u32 session_id;
    u32 data_len;
};

void tcp_receiver_sort(list<ref<tcp_client_ctx>>& client_list, rw_mtx& list_mtx)
{
    bool need_sort = false;

    rw_shared_guard sg(list_mtx);
    for (auto c : client_list)
    {
        if (c->sk <= 0)
        {
            need_sort = true;
            break;
        }
    }
    sg.unlock();

    if (!need_sort)
    {
        return;
    }

    rw_unique_guard ug(list_mtx);
    auto itr = client_list.begin();
    while (itr != client_list.end())
    {
        if (itr->get()->sk <= 0)
        {
            client_list.erase(itr);
            return;
        }
        ++itr;
    }
}

void tcp_receiver_one_client_runtime(ref<tcp_client_ctx> client_ctx, volatile bool& stop, atom<long long>& thread_cnt)
{
    ++thread_cnt;
    escape_function ef = [&]() mutable { --thread_cnt; };
    auto sk = client_ctx->sk;

    while (!stop)
    {
        int rst;
        fd_set sk_set;
        ::timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        FD_ZERO(&sk_set);
        FD_SET(sk, &sk_set);
        rst = ::select(int(sk + 1), &sk_set, nullptr, nullptr, &tv);
        if (rst < 0)
        {
            // error
            goto done;
        }
        if (rst == 0 || !FD_ISSET(sk, &sk_set))
        {
            // no data
            continue;
        }

        tcp_packet pkt;
        s64 recv_len = recv(client_ctx->sk, (char*)&pkt, sizeof(tcp_packet), 0);
        if (recv_len != sizeof(tcp_packet) || pkt.session_id != session_id || pkt.data_len <= 0)
        {
            // error
            goto done;
        }

        auto_memory am(pkt.data_len);
        s32 try_times = 0;
        recv_len = 0;
        while (recv_len < pkt.data_len)
        {
            FD_ZERO(&sk_set);
            FD_SET(sk, &sk_set);
            rst = ::select(int(sk + 1), &sk_set, nullptr, nullptr, &tv);
            if (rst < 0)
            {
                // error
                goto done;
            }
            if (rst == 0 || !FD_ISSET(sk, &sk_set))
            {
                // no data
                if (++try_times >= 50)
                {
                    goto done;
                }
                continue;
            }
            rst = recv(sk, am.get<char>() + recv_len, int(pkt.data_len - recv_len), 0);
            if (rst <= 0)
            {
                // error
                goto done;
            }
            recv_len += rst;
        }

        mutex_guard guard(client_ctx->list_mtx);
        client_ctx->memory_list.push_back(am);
    }

done:
    ::closesocket(client_ctx->sk);
    client_ctx->sk = 0;
}

void tcp_receiver_runtime(
    long long sk, list<ref<tcp_client_ctx>>& client_list, rw_mtx& list_mtx,
    volatile bool& stop, atom<long long>& thread_cnt)
{
    ++thread_cnt;
    escape_function ef = [&]() mutable { --thread_cnt; };

    fd_set sk_set;
    sockaddr addr;
    int addr_len = sizeof(addr);

    while (!stop)
    {
        int rst;
        ::timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        FD_ZERO(&sk_set);
        FD_SET(sk, &sk_set);

        rst = ::select(int(sk + 1), &sk_set, nullptr, nullptr, &tv);
        if (rst < 0)
        {
            // error
            stop = true;
            return;
        }
        if (rst == 0 || !FD_ISSET(sk, &sk_set))
        {
            // no new client
            tcp_receiver_sort(client_list, list_mtx);
            continue;
        }

        long long client = ::accept(sk, &addr, &addr_len);
        if (client <= 0)
        {
            // error
            stop = true;
            return;
        }

        ref<tcp_client_ctx> client_ctx(::new tcp_client_ctx());
        client_ctx->sk = client;

        rw_unique_guard guard(list_mtx);
        client_list.push_back(client_ctx);

        thread t(
            [&]() mutable
            {
                tcp_receiver_one_client_runtime(client_ctx, stop, thread_cnt);
            });

        t.detach();

    }
}

}

namespace tcp
{

using namespace ::es::_internal;

ref<receiver> receiver::build(int port)
{
    ref<receiver> r = ref<receiver>(::new receiver());
    return r->init(port) ? r : ref<receiver>();
}

receiver::~receiver()
{
    close();
}

bool receiver::read(long long& client_id, void* output_buf, long long& read_len)
{
    if (!output_buf)
    {
        return false;
    }
    if (_client_list.size() == 0)
    {
        return false;
    }

    rw_shared_guard sg(_client_list_mtx);
    for (auto c : _client_list)
    {
        auto client = c->sk;
        if (client <= 0)
        {
            continue;
        }
        if (c->memory_list.size() == 0)
        {
            continue;
        }
        if (!c->list_mtx.try_lock())
        {
            continue;
        }
        if (c->memory_list.size() == 0)
        {
            c->list_mtx.unlock();
            continue;
        }

        auto mem = c->memory_list.front();
        c->memory_list.pop_front();
        c->list_mtx.unlock();

        memcpy(output_buf, mem.get(), mem.size());
        read_len = mem.size();
        client_id = client;
        return true;
    }
    return false;
}

void receiver::close()
{
    _stop = true;
    while (_thread_cnt.load() > 0)
    {
        sleep_ms(50);
    }
    _stop = false;

    if (_sk > 0)
    {
        ::closesocket(_sk);
        _sk = 0;
    }
}

receiver::receiver() :
    _sk(0),
    _stop(false),
    _thread_cnt(0),
    _client_list(),
    _client_list_mtx()
{
}

bool receiver::init(int port)
{
    WSADATA wsa_data = {};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        cout << __FUNCTION__ << "::WSAStartup" << " Failed!" << endl;
        return false;
    }

    char host_name[256] = {};
    if (::gethostname(host_name, sizeof(host_name)) != 0)
    {
        cout << __FUNCTION__ << "::gethostname" << " Failed!" << endl;
        return false;
    }

    _sk = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_sk <= 0)
    {
        cout << __FUNCTION__ << "::socket" << " Failed!" << endl;
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
    addr.sin_port = ::htons((u_short)port);
    if (::bind(_sk, (SOCKADDR*)&addr, sizeof(SOCKADDR)) != 0)
    {
        cout << __FUNCTION__ << "::bind" << " Failed!" << endl;
        return false;
    }

    int sk_buf_len = 64 * 1024 * 1024;
    ::setsockopt(_sk, SOL_SOCKET, SO_RCVBUF, (char*)&sk_buf_len, sizeof(sk_buf_len));

    if (::listen(_sk, SOMAXCONN) != 0)
    {
        cout << __FUNCTION__ << "::listen" << " Failed!" << endl;
        return false;
    }

    thread t(
        [&]() mutable
        {
            tcp_receiver_runtime(_sk, _client_list, _client_list_mtx, _stop, _thread_cnt);
        }
    );

    t.detach();
    return true;
}

ref<sender> sender::build(const char* ip, int port)
{
    ref<sender> r = ref<sender>(::new sender());
    return r->init(ip, port) ? r : ref<sender>();
}

sender::~sender()
{
    if (_sk > 0)
    {
        ::closesocket(_sk);
        _sk = 0;
    }
}

bool sender::send(const void* buf, long long send_len)
{
    if (!buf || send_len <= 0)
    {
        return false;
    }

    tcp_packet pkt = {};
    pkt.session_id = session_id;
    pkt.data_len = (u32)send_len;

    mutex_guard mg(_mtx);
    if (::send(_sk, (const char*)&pkt, sizeof(tcp_packet), 0) != sizeof(tcp_packet))
    {
        return false;
    }
    if (::send(_sk, (const char*)buf, (int)send_len, 0) != send_len)
    {
        return false;
    }
    return true;
}

sender::sender() :
    _sk(0),
    _mtx()
{
}

bool sender::init(const char* ip, int port)
{
    WSADATA wsa_data = {};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        cout << __FUNCTION__ << "::WSAStartup" << " Failed!" << endl;
        return false;
    }

    _sk = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_sk == INVALID_SOCKET)
    {
        cout << __FUNCTION__ << "::socket" << " Failed!" << endl;
        return false;
    }

    int sk_buf_len = 64 * 1024;
    ::setsockopt(_sk, SOL_SOCKET, SO_SNDBUF, (char*)&sk_buf_len, sizeof(sk_buf_len));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr(ip);
    addr.sin_port = ::htons((u_short)port);
    if (::connect(_sk, (SOCKADDR*)&addr, sizeof(SOCKADDR)) != 0)
    {
        cout << __FUNCTION__ << "::connect" << " Failed!" << endl;
        return false;
    }

    return true;
}

}
}

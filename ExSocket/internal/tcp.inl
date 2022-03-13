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

}

using namespace es::_internal;

namespace tcp
{

inline ref<receiver> receiver::build(int port, notifier* notifier)
{
    if (port <= 0 || !notifier)
    {
        return ref<receiver>();
    }
    ref<receiver> r = ref<receiver>(::new receiver());
    return r->init(port, notifier) ? r : ref<receiver>();
}

inline receiver::~receiver()
{
    close();
}

inline void receiver::close()
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

inline receiver::receiver() :
    _sk(0),
    _stop(false),
    _thread_cnt(0),
    _notifier(nullptr)
{
}

inline bool receiver::init(int port, notifier* notifier)
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

    _notifier = notifier;
    ++_thread_cnt;
    thread t([&]() mutable { this->main_thread(); });
    t.detach();
    return true;
}

inline void receiver::main_thread()
{
    escape_function ef = [&]() mutable { --_thread_cnt; };

    fd_set sk_set;
    sockaddr addr;
    int addr_len = sizeof(addr);

    while (!_stop)
    {
        int rst;
        ::timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000 * socket_select_waiting_ms;
        FD_ZERO(&sk_set);
        FD_SET(_sk, &sk_set);

        rst = ::select(int(_sk + 1), &sk_set, nullptr, nullptr, &tv);
        if (rst < 0)
        {
            // error
            _stop = true;
            return;
        }
        if (rst == 0 || !FD_ISSET(_sk, &sk_set))
        {
            // no new client
            continue;
        }

        long long client = ::accept(_sk, &addr, &addr_len);
        if (client <= 0)
        {
            // error
            _stop = true;
            return;
        }

        ++_thread_cnt;
        thread t([&]() mutable { this->client_thread(client); });
        t.detach();
    }
}

inline void receiver::client_thread(long long client)
{
    char* buffer = new char[tcp_packet_max_len];
    escape_function ef =
        [&]() mutable
        {
            if (buffer)
            {
                delete[] buffer;
            }
            ::closesocket(client);
            _notifier->on_client_disconnect(client);
            --_thread_cnt;
        };

    if (!buffer)
    {
        return;
    }
    _notifier->on_client_connect(client);
    while (!_stop)
    {
        int rst;
        fd_set sk_set;
        ::timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000 * socket_select_waiting_ms;

        FD_ZERO(&sk_set);
        FD_SET(client, &sk_set);
        rst = ::select(int(client + 1), &sk_set, nullptr, nullptr, &tv);
        if (rst < 0)
        {
            // error
            return;
        }
        if (rst == 0 || !FD_ISSET(client, &sk_set))
        {
            // no data
            continue;
        }
        tcp_packet pkt;
        s64 recv_len = ::recv((int)client, (char*)&pkt, sizeof(tcp_packet), 0);
        if (recv_len != sizeof(tcp_packet) ||
            pkt.session_id != session_id ||
            pkt.data_len <= 0 || pkt.data_len > tcp_packet_max_len)
        {
            // error
            return;
        }
        s32 try_times = 0;
        recv_len = 0;
        while (recv_len < pkt.data_len)
        {
            FD_ZERO(&sk_set);
            FD_SET(client, &sk_set);
            rst = ::select(int(client + 1), &sk_set, nullptr, nullptr, &tv);
            if (rst < 0)
            {
                // error
                return;
            }
            if (rst == 0 || !FD_ISSET((int)client, &sk_set))
            {
                // no data
                if (++try_times >= 20)
                {
                    return;
                }
                continue;
            }
            rst = ::recv((int)client, buffer + recv_len, int(pkt.data_len - recv_len), 0);
            if (rst <= 0)
            {
                // error
                return;
            }
            recv_len += rst;
        }
        _notifier->on_message_arrive(client, buffer, pkt.data_len);
    }
}

inline ref<sender> sender::build(const char* ip, int port)
{
    if (!ip || port <= 0)
    {
        return ref<sender>();
    }
    ref<sender> r = ref<sender>(::new sender());
    return r->init(ip, port) ? r : ref<sender>();
}

inline sender::~sender()
{
    if (_sk > 0)
    {
        ::closesocket(_sk);
        _sk = 0;
    }
}

inline bool sender::send(const void* buf, long long send_len)
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

inline sender::sender() :
    _sk(0),
    _mtx()
{
}

inline bool sender::init(const char* ip, int port)
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

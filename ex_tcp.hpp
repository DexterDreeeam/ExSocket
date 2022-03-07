#pragma once

#include "ex_common.hpp"

namespace Es
{

namespace Tcp
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
struct Packet
{
    u32  session_id, data_len;
};

 // ================== custom ==================
const u32  session_id = 1234;

class Receiver
{
public:
    static ref<Receiver> Build(u32 port)
    {
        auto r = ref<Receiver>(new Receiver());
        return r->init(port) ? r : ref<Receiver>();
    }

    ~Receiver()
    {
        if (_sk)
        {
            closesocket(_sk);
            _sk = 0;
        }
    }

    SOCKET WaitClient()
    {
        sockaddr _addr;
        SOCKET client_sk = accept(_sk, &_addr, nullptr);
        if (client_sk < 0)
        {
            return -1;
        }
        return client_sk;
    }

    int Read(SOCKET client_sk, void* buf)
    {
        Packet header;
        int recv_len = recv(client_sk, (char*)&header, sizeof(Packet), 0);
        if (recv_len < 0 || header.data_len <= 0)
        {
            closesocket(client_sk);
            return -1;
        }

        bool is_success = recv(client_sk, (char*)buf, header.data_len, 0) == header.data_len;
        if (!is_success)
        {
            closesocket(client_sk);
            return -1;
        }

        return (int)header.data_len;
    }

private:
    Receiver() = default;

    bool init(u32 port)
    {
        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            cout << __FUNCTION__ << "::WSAStartup" << " Failed!" << endl;
            return false;
        }

        char host_name[256] = {};
        if (gethostname(host_name, sizeof(host_name)) != 0)
        {
            cout << __FUNCTION__ << "::gethostname" << " Failed!" << endl;
            return false;
        }

        _sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sk == INVALID_SOCKET)
        {
            cout << __FUNCTION__ << "::socket" << " Failed!" << endl;
            return false;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((u_short)port);
        if (bind(_sk, (SOCKADDR*)&addr, sizeof(SOCKADDR)) != 0)
        {
            cout << __FUNCTION__ << "::bind" << " Failed!" << endl;
            return false;
        }

        int sk_buf_len = 64 * 1024 * 1024;
        setsockopt(_sk, SOL_SOCKET, SO_RCVBUF, (char*)&sk_buf_len, sizeof(sk_buf_len));

        if (listen(_sk, SOMAXCONN) != 0)
        {
            cout << __FUNCTION__ << "::listen" << " Failed!" << endl;
            return false;
        }

        return true;
    }

private:
    SOCKET _sk;
};

class Sender
{
public:
    static ref<Sender> Build(const char* ip_cstr, u32 port)
    {
        auto s = ref<Sender>(new Sender());
        return s->init(ip_cstr, port) ? s : ref<Sender>();
    }

    ~Sender()
    {
        if (_sk)
        {
            closesocket(_sk);
            _sk = INVALID_SOCKET;
        }
    }

    bool Send(const void* buf, u32 send_len)
    {
        if (_sk == INVALID_SOCKET || !buf || send_len == 0)
        {
            return false;
        }

        Packet packet = {};
        packet.session_id = session_id;
        packet.data_len = send_len;

        if (send(_sk, (const char*)&packet, sizeof(Packet), 0) != sizeof(Packet))
        {
            return false;
        }
        if (send(_sk, (const char*)buf, send_len, 0) != send_len)
        {
            return false;
        }
        return true;
    }

private:
    Sender() = default;

    bool init(const char* ip_addr, int port)
    {
        _sk = INVALID_SOCKET;

        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            cout << __FUNCTION__ << "::WSAStartup" << " Failed!" << endl;
            return false;
        }

        _sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sk == INVALID_SOCKET)
        {
            cout << __FUNCTION__ << "::socket" << " Failed!" << endl;
            return false;
        }

        int sk_buf_len = 64 * 1024;
        setsockopt(_sk, SOL_SOCKET, SO_SNDBUF, (char*)&sk_buf_len, sizeof(sk_buf_len));

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(ip_addr);
        addr.sin_port = htons((u_short)port);
        if (connect(_sk, (SOCKADDR*)&addr, sizeof(SOCKADDR)) != 0)
        {
            cout << __FUNCTION__ << "::connect" << " Failed!" << endl;
            return false;
        }

        return true;
    }

private:
    SOCKET       _sk;
};

}
}

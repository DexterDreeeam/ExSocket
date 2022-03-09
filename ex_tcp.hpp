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
    class ThreadHandler
    {
    public:
        static ref<ThreadHandler> Build(atom<s32>& counter)
        {
            return ref<ThreadHandler>(::new ThreadHandler(counter));
        }

        ~ThreadHandler() noexcept
        {
            --_counter;
        }

    protected:
        ThreadHandler(atom<s32>& counter) :
            _counter(counter)
        {
            ++_counter;
        }

        atom<s32>&  _counter;
    };

public:
    static ref<Receiver> Build(u32 port)
    {
        auto r = ref<Receiver>(new Receiver());
        return r->Init(port) ? r : ref<Receiver>();
    }

    ~Receiver()
    {
        _stop_token = true;
        while (_thread_counter.load() > 0)
        {
            sleep_ms(50);
        }
        if (_sk)
        {
            closesocket(_sk);
            _sk = 0;
        }
    }

    int Read(SOCKET client_sk, void* buf)
    {
        Packet header;
        s32 recv_len = recv(client_sk, (char*)&header, sizeof(Packet), 0);
        if (recv_len < 0 || header.data_len <= 0)
        {
            closesocket(client_sk);
            return -1;
        }

        s32 try_times = 0;
        recv_len = 0;

        while (1)
        {
            s32 len = recv(client_sk, (char*)buf + recv_len, header.data_len - recv_len, 0);
            if (len > 0)
            {
                recv_len += len;
                try_times = 0;
            }
            ++try_times;
            if (recv_len >= (s32)header.data_len)
            {
                break;
            }
            sleep_us(50);
            if (try_times >= 30)
            {
                closesocket(client_sk);
                return -1;
            }
        }
        return (int)header.data_len;
    }

private:
    Receiver() :
        _sk(0),
        _stop_token(false),
        _thread_counter(0)
    {
    }

    bool Init(u32 port)
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

        auto th = ThreadHandler::Build(_thread_counter);
        thread t([=]() mutable { this->RunningThread(th); });
        t.detach();

        return true;
    }

    void RunningThread(ref<ThreadHandler> th)
    {
        th;
        while (!_stop_token)
        {
            timeval tv = { 0, 100 };
            fd_set skset;
            FD_ZERO(&skset);
            FD_SET(_sk, &skset);
            if (select(0, &skset, nullptr, nullptr, &tv) <= 0)
            {
                sleep_ms(200);
                continue;
            }
            sockaddr _addr;
            auto client_sk = accept(_sk, &_addr, nullptr);
            if (client_sk <= 0)
            {
                sleep_ms(200);
                continue;
            }
            ListenClient(client_sk);
        }
    }

    void ListenClient(SOCKET client_sk)
    {
        escape_function ef =
            [=](){ closesocket(client_sk); };

        while (!_stop_token)
        {
            Packet header;
            s32 recv_len = recv(client_sk, (char*)&header, sizeof(Packet), 0);
            if (recv_len == 0)
            {
                sleep_ms(50);
                continue;
            }
            if (recv_len < 0 || header.data_len <= 0)
            {
                return;
            }

            auto mem = auto_memory::Build(1024 * 1024 * 4);
            s32 try_times = 0;
            recv_len = 0;
            while (1)
            {
                s32 len = recv(client_sk, (char*)buf + recv_len, header.data_len - recv_len, 0);
                if (len > 0)
                {
                    recv_len += len;
                    try_times = 0;
                }
                ++try_times;
                if (recv_len >= (s32)header.data_len)
                {
                    break;
                }
                sleep_us(50);
                if (try_times >= 30)
                {
                    closesocket(client_sk);
                    return -1;
                }
            }
            return (int)header.data_len;
        }
    }

private:
    SOCKET         _sk;
    volatile bool  _stop_token;
    atom<s32>      _thread_counter;
    
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

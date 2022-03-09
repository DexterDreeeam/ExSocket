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

    int Read(void* buf)
    {
        //auto mem = ReadMemory();
        //if (mem)
        //{
        //    memcpy(buf, mem.get(), mem.size());
        //    return mem.size();
        //}
        //else
        //{
        //    return 0;
        //}
    }

    auto Read()
    {
        return ReadMemory();
    }

    int Count()
    {
        //return Count(_memory_arr_begin.load());
    }

    bool IsConnecting() const
    {
        return _is_connecting;
    }

private:
    Receiver() :
        _sk(0),
        _stop_token(false),
        _thread_counter(0),
        _is_connecting(false)//,
        //_memory_arr(),
        //_memory_arr_begin(0),
        //_memory_arr_end(0)
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

        thread t([=]() mutable { this->RunningThread(); });
        t.detach();

        return true;
    }

    void RunningThread()
    {
        escape_function ef = [=]() { --_thread_counter; };
        ++_thread_counter;

        while (!_stop_token)
        {
            //timeval tv = { 0, 100 };
            //fd_set skset;
            //FD_ZERO(&skset);
            //FD_SET(_sk, &skset);
            //if (select(0, &skset, nullptr, nullptr, &tv) <= 0)
            //{
            //    sleep_ms(200);
            //    continue;
            //}
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

    //void ListenClient(SOCKET client_sk)
    //{
    //    escape_function ef =
    //        [=]()
    //        {
    //            _is_connecting = false;
    //            closesocket(client_sk);
    //        };
    //    _is_connecting = true;
    //    Packet header;
    //    while (!_stop_token)
    //    {
    //        s32 recv_len = recv(client_sk, (char*)&header, sizeof(Packet), 0);
    //        if (recv_len == 0)
    //        {
    //            sleep_us(50);
    //            continue;
    //        }
    //        if (recv_len < 0 || header.data_len <= 0)
    //        {
    //            // receive packet fail
    //            return;
    //        }
    //        auto mem = auto_memory(header.data_len + 64);
    //        if (!mem)
    //        {
    //            // allocate memory fail
    //            return;
    //        }
    //        s32 try_times = 0;
    //        recv_len = 0;
    //        while (1)
    //        {
    //            s32 len = recv(client_sk, mem.get<char>() + recv_len, header.data_len - recv_len, 0);
    //            if (len > 0)
    //            {
    //                recv_len += len;
    //                try_times = 0;
    //            }
    //            ++try_times;
    //            if (recv_len >= (s32)header.data_len)
    //            {
    //                // packet data receive complete
    //                break;
    //            }
    //            if (_stop_token || try_times >= 80)
    //            {
    //                return;
    //            }
    //            sleep_us(50);
    //        }
    //        //u32 end_idx = _memory_arr_end++;
    //        //_memory_arr[end_idx % _memory_arr_len] = mem;
    //        mutex_guard mg(_memory_list_mtx);
    //        _memory_list.push_back(mem);
    //    }
    //}

    void ListenClient(SOCKET client_sk)
    {
        escape_function ef =
            [=]()
        {
            _is_connecting = false;
            closesocket(client_sk);
        };
        _is_connecting = true;
        Packet header;
        while (!_stop_token)
        {
            s32 recv_len = recv(client_sk, (char*)&header, sizeof(Packet), 0);
            if (recv_len == 0)
            {
                sleep_us(50);
                continue;
            }
            if (recv_len < 0 || header.data_len <= 0)
            {
                // receive packet fail
                return;
            }
            char* mem = new char[header.data_len + 128];
            memset(mem + header.data_len, 0, 128);
            if (!mem)
            {
                // allocate memory fail
                return;
            }
            s32 try_times = 0;
            recv_len = 0;

                s32 len = recv(client_sk, mem, header.data_len, 0);

            mutex_guard mg(_memory_list_mtx);
            _memory_list.push_back(pair<int, char*>(recv_len, mem));
        }
    }

    int Count(u32 begin_idx) const
    {
        //u64 end_idx = (u64)_memory_arr_end + (_memory_arr_len << 8);
        //return (end_idx - begin_idx) % _memory_arr_len;
    }

    //auto_memory ReadMemory()
    //{
    //    while (1)
    //    {
    //        u32 begin_idx = _memory_arr_begin.load();
    //        if (!Count(begin_idx))
    //        {
    //            return auto_memory();
    //        }
    //        if (_memory_arr_begin.compare_exchange_strong(begin_idx, begin_idx + 1))
    //        {
    //            return _memory_arr[begin_idx % _memory_arr_len];
    //        }
    //    }
    //}

    pair<int, char*> ReadMemory()
    {
        pair<int, char*> ret = { 0, nullptr };
        mutex_guard mg(_memory_list_mtx);
        if (_memory_list.size())
        {
            ret = _memory_list.front();
            _memory_list.pop_front();
        }
        return ret;
    }

public:
    static const u32 _memory_arr_len = 1 << 8;

private:
    SOCKET            _sk;
    volatile bool     _stop_token;
    atom<s32>         _thread_counter;
    volatile bool     _is_connecting;
    //auto_memory       _memory_arr[_memory_arr_len];
    //atom<u32>         _memory_arr_begin;
    //volatile u32      _memory_arr_end;

    list<pair<int, char*>>    _memory_list;
    mutex             _memory_list_mtx;
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

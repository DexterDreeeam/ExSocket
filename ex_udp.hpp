#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <winsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma warning (disable : 4200)

namespace Eu
{

using u8  = std::uint8_t;
using s8  = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

using thread          = std::thread;
using mutex           = std::mutex;
using mutex_guard     = std::unique_lock<std::mutex>;
using rw_mtx          = std::shared_mutex;
using rw_unique_guard = std::unique_lock<std::shared_mutex>;
using rw_shared_guard = std::shared_lock<std::shared_mutex>;

template<typename T>               using atom = std::atomic<T>;
template<typename T1, typename T2> using pair = std::pair<T1, T2>;
template<typename T>               using vec  = std::vector<T>;
template<typename T>               using list = std::list<T>;
template<typename T>               using ref  = std::shared_ptr<T>;

void yield() { std::this_thread::yield(); }
void sleep_ms(u32 s) { std::this_thread::sleep_for(std::chrono::milliseconds(s)); }
void sleep_us(u32 s) { std::this_thread::sleep_for(std::chrono::microseconds(s)); }

/*
 *   ·--------------------------------------·
 *   |    session_id    |  numor  |  detor  |
 *   |--------------------------------------|
 *   |                index                 |
 *   |--------------------------------------|
 *   |-                                    -|
 *   |-                DATA                -|
 *   |-                                    -|
 *   ·--------------------------------------·
 */

struct Packet
{
    u32  session_id;
    u16  numor, detor;
    u64  index;
    u8   data[0];
};

// ================== custom ==================
const bool print_msg              = true;
const u32  session_id             = 1234;
const u32  udp_mtu                = 1 << 10;
const u32  waiting_window         = 16;
const u32  receiver_thread_cnt    = 6;
const u32  socket_receive_buf_mb  = 128;
const u32  socket_send_buf_mb     = 64;

// ***************** constant *****************
const u32  udp_mtu_min         = 1 << 9;
const u32  udp_mtu_max         = 1 << 12;
const u32  thread_cnt_min      = 1;
const u32  thread_cnt_max      = 8;
const u32  data_max_len        = udp_mtu - sizeof(Packet);
const u32  socket_receive_buf  = (1024 * 1024) * socket_receive_buf_mb;
const u32  socket_send_buf     = (1024 * 1024) * socket_send_buf_mb;

struct _eu_endl {} __declspec(selectany) endl;

class _eu_cout
{
public:
    _eu_cout() = default;
    ~_eu_cout() = default;

    template<typename Ty>
    _eu_cout operator <<(const Ty& x)
    {
        if (print_msg)
        {
            std::cout << x;
        }
        return *this;
    }

    _eu_cout operator <<(const _eu_endl&)
    {
        if (print_msg)
        {
            std::cout << std::endl;
        }
        return *this;
    }
};

__declspec(selectany) _eu_cout cout;

struct PacketWrapper
{
    ref<Packet> pkt;
    u32         size;
};

class PacketGroup
{
public:
    PacketGroup() = delete;

    ~PacketGroup() = default;

    PacketGroup(PacketWrapper pw) :
        _index(pw.pkt->index),
        _pkt_total(pw.pkt->detor),
        _pkt_cnt(1),
        _size(pw.size),
        _list(1, pw),
        _list_mtx()
    {
    }

    void Enqueue(PacketWrapper pw)
    {
        mutex_guard gd(_list_mtx);
        _list.push_back(pw);
        gd.unlock();

        _size += pw.size;
        ++_pkt_cnt;
    }

    u64 GetSizeIfReady() const
    {
        return _pkt_cnt.load() != _pkt_total ? 0 : _size.load();
    }

    void ReadMemmory(void* mem)
    {
        mutex_guard gd(_list_mtx);
        _list.sort(
            [](PacketWrapper& pw1, PacketWrapper& pw2)
            {
                return pw1.pkt->numor < pw2.pkt->numor;
            });

        char* pMem = (char*)mem;
        for (auto& pw : _list)
        {
            memcpy(pMem, pw.pkt->data, pw.size);
            pMem += pw.size;
        }
    }

    u64 Index() const
    {
        return _index;
    }

private:
    const u64           _index;
    u64                 _pkt_total;
    atom<u64>           _pkt_cnt;
    atom<u64>           _size;
    list<PacketWrapper> _list;
    mutex               _list_mtx;
};

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
        _thread_terminating.store(true);
        while (_thread_list.size())
        {
            _thread_list.back()->join();
            _thread_list.pop_back();
        }
        if (_sk)
        {
            closesocket(_sk);
            _sk = 0;
        }
    }

    u64 Read(void* mem, bool remove_previous_pg = false)
    {
        auto pg = get_ready_pg(remove_previous_pg);
        if (pg)
        {
            pg->ReadMemmory(mem);
            return pg->GetSizeIfReady();
        }
        return 0;
    }

private:
    Receiver() = default;

    bool init(u32 port)
    {
        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            cout << __FUNCTION__ << "::WSAStartup()" << " Failed!" << endl;
            return false;
        }

        char host_name[256] = {};
        if (gethostname(host_name, sizeof(host_name)) != 0)
        {
            cout << __FUNCTION__ << "::gethostname()" << " Failed!" << endl;
            return false;
        }

        _sk = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_sk == INVALID_SOCKET)
        {
            cout << __FUNCTION__ << "::socket()" << " Failed!" << endl;
            return false;
        }

        _addr.sin_family = AF_INET;
        _addr.sin_addr.s_addr = htonl(INADDR_ANY);
        _addr.sin_port = htons((u_short)port);
        if (bind(_sk, (SOCKADDR*)&_addr, sizeof(SOCKADDR)) != 0)
        {
            cout << __FUNCTION__ << "::bind()" << " Failed!" << endl;
            return false;
        }

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(_sk, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        setsockopt(_sk, SOL_SOCKET, SO_RCVBUF, (char*)&socket_receive_buf, sizeof(socket_receive_buf));

        _thread_terminating = false;
        for (u32 i = 0; i < receiver_thread_cnt; ++i)
        {
            auto t = ref<thread>(new thread(
                [&]() mutable
                { this->thread_loop(); }));

            _thread_list.push_back(t);
        }
        return true;
    }

    void thread_loop()
    {
        while (_thread_terminating.load() == false)
        {
            s32 addr_len = sizeof(_addr);
            auto pkt = ref<Packet>((Packet*)malloc(udp_mtu_max));
            s32 pkt_len = recvfrom(_sk, (char*)pkt.get(), udp_mtu_max, 0, (sockaddr*)&_addr, &addr_len);
            if (pkt_len <= 0)
            {
                sleep_ms(1);
                continue;
            }

            PacketWrapper pw = { pkt, (u32)pkt_len };
            auto pg = get_pg(pkt->index);
            if (pg)
            {
                pg->Enqueue(pw);
            }
            else
            {
                create_pg(pw);
            }

            if (check_pg_list_overflow())
            {
                trim_pg_list();
            }
        }
    }

    ref<PacketGroup> get_pg(u64 index)
    {
        ref<PacketGroup> ret;
        rw_shared_guard gd(_pg_list_mtx);
        for (auto pg : _pg_list)
        {
            if (pg->Index() == index)
            {
                ret = pg;
                break;
            }
        }
        return ret;
    }

    void create_pg(const PacketWrapper& pw)
    {
        rw_unique_guard gd(_pg_list_mtx);
        for (auto pg : _pg_list)
        {
            if (pg->Index() == pw.pkt->index)
            {
                gd.unlock();
                pg->Enqueue(pw);
                return;
            }
        }

        auto pg = ref<PacketGroup>(new PacketGroup(pw));
        auto itr = _pg_list.begin();
        for (; itr != _pg_list.end(); ++itr)
        {
            if (itr->get()->Index() > pw.pkt->index)
            {
                break;
            }
        }
        _pg_list.insert(itr, pg);
    }

    bool check_pg_list_overflow()
    {
        bool ret = false;
        rw_shared_guard gd(_pg_list_mtx);
        ret = _pg_list.size() > waiting_window;
        return ret;
    }

    void trim_pg_list()
    {
        rw_unique_guard gd(_pg_list_mtx);
        while (_pg_list.size() > waiting_window)
        {
            _pg_list.pop_front();
        }
    }

    ref<PacketGroup> get_ready_pg(bool remove_previous_pg)
    {
        ref<PacketGroup> ret;
        decltype(_pg_list.begin()) itr;

        rw_shared_guard s_gd(_pg_list_mtx);
        for (itr = _pg_list.begin(); itr != _pg_list.end(); ++itr)
        {
            if (itr->get()->GetSizeIfReady())
            {
                break;
            }
        }
        if (itr == _pg_list.end())
        {
            return ret;
        }
        s_gd.unlock();

        rw_unique_guard u_gd(_pg_list_mtx);
        for (itr = _pg_list.begin(); itr != _pg_list.end(); ++itr)
        {
            if (itr->get()->GetSizeIfReady())
            {
                break;
            }
        }
        if (itr == _pg_list.end())
        {
            return ret;
        }
        ret = *itr;
        _pg_list.erase(itr);
        while (remove_previous_pg && _pg_list.begin() != itr)
        {
            _pg_list.pop_front();
        }
        return ret;
    }

private:
    SOCKET                  _sk;
    sockaddr_in             _addr;
    list<ref<PacketGroup>>  _pg_list;
    rw_mtx                  _pg_list_mtx;
    vec<ref<thread>>        _thread_list;
    atom<bool>              _thread_terminating;
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
            _sk = 0;
        }
    }

    bool Send(const void* buf, u64 send_len)
    {
        if (!buf || send_len == 0)
        {
            return false;
        }
        u64 index = ++_index;
        u64 packet_split_cnt = (send_len + data_max_len - 1) / data_max_len;
        const char* pBuf = (const char*)buf;
        u64 data_len;
        u32 numor = 0;

        Packet* pkt = (Packet*)_buf;
        pkt->session_id = session_id;
        pkt->numor = numor;
        pkt->detor = (u16)packet_split_cnt;
        pkt->index = index;
        data_len = send_len <= data_max_len ? send_len : data_max_len;
        memcpy(pkt->data, buf, data_len);
        if (!udp_send(pkt, sizeof(Packet) + data_len))
        {
            cout << __FUNCTION__ << "::udp_send()" << " Failed!" << endl;
            return false;
        }
        send_len -= data_len;
        pBuf += data_len;
        ++numor;

        while (send_len > 0)
        {
            pkt = (Packet*)(pBuf - sizeof(Packet));
            pkt->session_id = session_id;
            pkt->numor = numor;
            pkt->detor = (u16)packet_split_cnt;;
            pkt->index = index;
            data_len = send_len <= data_max_len ? send_len : data_max_len;
            if (!udp_send(pkt, sizeof(Packet) + data_len))
            {
                cout << __FUNCTION__ << "::udp_send()" << " Failed!" << endl;
                return false;
            }
            send_len -= data_len;
            pBuf += data_len;
            ++numor;
        }

        return true;
    }

private:
    Sender() = default;

    bool init(const char* ip_addr, int port)
    {
        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            cout << __FUNCTION__ << "::WSAStartup()" << " Failed!" << endl;
            return false;
        }

        _sk = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (_sk == INVALID_SOCKET)
        {
            cout << __FUNCTION__ << "::socket()" << " Failed!" << endl;
            return false;
        }

        setsockopt(_sk, SOL_SOCKET, SO_SNDBUF, (char*)&socket_send_buf, sizeof(socket_send_buf));

        _addr.sin_family = AF_INET;
        _addr.sin_addr.s_addr = inet_addr(ip_addr);
        _addr.sin_port = htons((u_short)port);
        _index.store(0);

        return true;
    }

    bool udp_send(const void* buf, u64 send_len)
    {
        return sendto(_sk, (const char*)buf, (int)send_len, 0, (sockaddr*)&_addr, sizeof(SOCKADDR_IN)) > 0;
    }

private:
    SOCKET       _sk;
    sockaddr_in  _addr;
    atom<u64>    _index;
    char         _buf[udp_mtu];
};

}

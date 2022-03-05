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

using thread     = std::thread;
using shared_mtx = std::shared_mutex;
using mutex      = std::mutex;
using guard      = std::lock_guard<std::mutex>;

template<typename T>               using atom = std::atomic<T>;
template<typename T1, typename T2> using pair = std::pair<T1, T2>;
template<typename T>               using vec  = std::vector<T>;
template<typename T>               using list = std::list<T>;
template<typename T>               using ref  = std::shared_ptr<T>;

// ================== custom ==================
const bool print_msg           = true;
const u32  session_id          = 1234;
const u32  udp_mtu             = 1 << 10;
const u32  waiting_window      = 8;
const u32  thread_cnt          = 4;
const s32  socket_receive_buf  = 32 * 1024 * 1024;
const s32  socket_send_buf     = 16 * 1024 * 1024;

// ***************** constant *****************
const u32  udp_mtu_min         = 1 << 9;
const u32  udp_mtu_max         = 1 << 12;
const u32  thread_cnt_min      = 1;
const u32  thread_cnt_max      = 8;

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
        {
            guard gd(_list_mtx);
            _list.push_back(pw);
        }
        _size += pw.size;
        ++_pkt_cnt;
    }

    u32 GetSizeIfReady() const
    {
        return _pkt_cnt.load() != _pkt_total ? 0 : _size.load();
    }

    void ReadMemmory(void* mem)
    {
        guard gd(_list_mtx);
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
    u32                 _pkt_total;
    atom<u32>           _pkt_cnt;
    atom<u32>           _size;
    list<PacketWrapper> _list;
    mutex               _list_mtx;
};

class Receiver
{
public:
    static ref<Receiver> Build(u32 port)
    {
        auto r = ref<Receiver>(new Receiver());
        r->init(port) ? r : ref<Receiver>();
    }

    Receiver() = default;

    ~Receiver()
    {
        if (_sk)
        {
            closesocket(_sk);
            _sk = 0;
        }
    }

    u64 Read(void* mem)
    {
        
    }

private:
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
        tv.tv_sec = 4;
        tv.tv_usec = 0;
        setsockopt(_sk, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        setsockopt(_sk, SOL_SOCKET, SO_RCVBUF, (char*)&socket_receive_buf, sizeof(socket_receive_buf));

        for (u32 i = 0; i < thread_cnt; ++i)
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
        while (1)
        {
            s32 addr_len = sizeof(_addr);
            auto pkt = ref<Packet>((Packet*)malloc(udp_mtu_max));
            s32 pkt_len = recvfrom(_sk, (char*)pkt.get(), udp_mtu_max, 0, (sockaddr*)&_addr, &addr_len);

            PacketWrapper pw = { pkt, pkt_len };
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
        _pg_list_mtx.lock_shared();
        for (auto pg : _pg_list)
        {
            if (pg->Index() == index)
            {
                ret = pg;
                break;
            }
        }
        _pg_list_mtx.unlock_shared();
        return ret;
    }

    void create_pg(const PacketWrapper& pw)
    {
        _pg_list_mtx.lock();
        auto pg = get_pg(pw.pkt->index);
        if (pg)
        {
            _pg_list_mtx.unlock();
            pg->Enqueue(pw);
            return;
        }

        pg = ref<PacketGroup>(new PacketGroup(pw));
        auto itr = _pg_list.begin();
        for (; itr != _pg_list.end(); ++itr)
        {
            if (itr->get()->Index() > pw.pkt->index)
            {
                break;
            }
        }
        _pg_list.insert(itr, pg);
        _pg_list_mtx.unlock();
    }

    bool check_pg_list_overflow()
    {
        bool ret = false;
        _pg_list_mtx.lock_shared();
        ret = _pg_list.size() > waiting_window;
        _pg_list_mtx.unlock_shared();
        return ret;
    }

    void trim_pg_list()
    {
        _pg_list_mtx.lock();
        while (_pg_list.size() > waiting_window)
        {
            _pg_list.pop_front();
        }
        _pg_list_mtx.unlock();
    }

    ref<PacketGroup> get_ready_pg(bool remove_previous_pg = false)
    {
        
    }

private:
    SOCKET                  _sk;
    sockaddr_in             _addr;
    list<ref<PacketGroup>>  _pg_list;
    shared_mtx              _pg_list_mtx;
    vec<ref<thread>>        _thread_list;
};

//class Sender
//{
//public:
//    Sender() = default;
//
//    ~Sender() = default;
//
//public:
//    bool Init(const char* ip_addr, int port)
//    {
//        WSADATA wsa_data = {};
//        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
//        {
//            cout << __FUNCTION__ << "::WSAStartup" << " Failed!" << endl;
//            return false;
//        }
//
//        _sk = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//        if (_sk == INVALID_SOCKET)
//        {
//            cout << __FUNCTION__ << "::socket" << " Failed!" << endl;
//            return false;
//        }
//
//        _addr.sin_family = AF_INET;
//        _addr.sin_addr.s_addr = inet_addr(ip_addr);
//        _addr.sin_port = htons((u_short)port);
//        return true;
//    }
//
//    void Uninit()
//    {
//        closesocket(_sk);
//    }
//
//    bool Send(const void* buf, int len)
//    {
//        static std::atomic<unsigned long long> index = 0;
//
//        unsigned long long myIndex = ++index;
//        const char* pBuf = (const char*)buf;
//        char myBuf[65535] = {};
//        int packet_count = (len + max_udp_packet_size - 1) / max_udp_packet_size;
//        unsigned short numor = 0;
//        while (len > 0)
//        {
//            int send_len = len <= max_udp_packet_size ? len : max_udp_packet_size;
//
//            Packet* p = (Packet*)myBuf;
//            p->session_id = 1234;
//            p->numor = numor++;
//            p->detor = (unsigned short)packet_count;
//            p->index = myIndex;
//            p->format = 1234;
//            p->width = 640;
//            p->height = 480;
//            memcpy_s(p->data, send_len, pBuf, send_len);
//
//            if (sendto(_sk, myBuf, sizeof(Packet) + send_len, 0, (sockaddr*)&_addr, sizeof(SOCKADDR_IN)) < 0)
//            {
//                //cout << __FUNCTION__ << "::sendto" << " Failed!" << endl;
//                return false;
//            }
//            len -= send_len;
//            pBuf += send_len;
//        }
//        return true;
//    }
//
//private:
//    SOCKET      _sk;
//    sockaddr_in _addr;
//};

}

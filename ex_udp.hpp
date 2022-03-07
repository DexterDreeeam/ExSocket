#pragma once

#include "ex_common.hpp"

namespace Es
{

namespace Udp
{

/*
 *   �--------------------------------------�
 *   |    session_id    |  numor  |  detor  |
 *   |--------------------------------------|
 *   |                index                 |
 *   |--------------------------------------|
 *   |-                                    -|
 *   |-                DATA                -|
 *   |-                                    -|
 *   �--------------------------------------�
 */

struct Packet
{
    u32  session_id;
    u16  numor, detor;
    u64  index;
    u8   data[0];
};

// ================== custom ==================
const u32  session_id = 1234;
const u32  udp_mtu = 1 << 10;
const u32  waiting_window = 64;
const u32  receiver_thread_cnt = 8;
const u32  socket_receive_buf_mb = 32;
const u32  socket_send_buf_mb = 4;

// ***************** constant *****************
const u32  udp_mtu_min = 1 << 9;
const u32  udp_mtu_max = 1 << 12;
const u32  thread_cnt_min = 1;
const u32  thread_cnt_max = 8;
const u32  data_max_len = udp_mtu - sizeof(Packet);
const u32  socket_receive_buf = (1024 * 1024) * socket_receive_buf_mb;
const u32  socket_send_buf = (1024 * 1024) * socket_send_buf_mb;

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
                [&, i]() mutable
                { this->thread_loop(i); }));

            _thread_list.push_back(t);
        }
        return true;
    }

    void thread_loop(u32 thread_id)
    {
        while (_thread_terminating.load() == false)
        {
            s32 addr_len = sizeof(_addr);
            auto pkt = ref<Packet>((Packet*)malloc(udp_mtu_max));
            s32 pkt_len = recvfrom(_sk, (char*)pkt.get(), udp_mtu_max, 0, (sockaddr*)&_addr, &addr_len);
            if (pkt_len <= 0)
            {
                sleep_us(50 + thread_id);
                continue;
            }

            PacketWrapper pw = { pkt, (u32)pkt_len };
            auto pg = get_pg(pkt->index);
            if (pg)
            {
                pg->Enqueue(pw);
                continue;
            }

            if (!create_pg(pw))
            {
                continue;
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

    bool create_pg(const PacketWrapper& pw)
    {
        rw_unique_guard gd(_pg_list_mtx);
        //if (_pg_list.size() && _pg_list.rbegin()->get()->Index() >= pw.pkt->index + waiting_window)
        //{
        //    return false;
        //}
        for (auto pg : _pg_list)
        {
            if (pg->Index() == pw.pkt->index)
            {
                gd.unlock();
                pg->Enqueue(pw);
                return false;
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
        return true;
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
}

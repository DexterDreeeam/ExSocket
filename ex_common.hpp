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

namespace Es
{

using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

using thread = std::thread;
using mutex = std::mutex;
using mutex_guard = std::unique_lock<std::mutex>;
using rw_mtx = std::shared_mutex;
using rw_unique_guard = std::unique_lock<std::shared_mutex>;
using rw_shared_guard = std::shared_lock<std::shared_mutex>;

template<typename T>               using atom = std::atomic<T>;
template<typename T1, typename T2> using pair = std::pair<T1, T2>;
template<typename T>               using vec = std::vector<T>;
template<typename T>               using list = std::list<T>;
template<typename T>               using ref = std::shared_ptr<T>;

void yield() { std::this_thread::yield(); }
void sleep_ms(u32 s) { std::this_thread::sleep_for(std::chrono::milliseconds(s)); }
void sleep_us(u32 s) { std::this_thread::sleep_for(std::chrono::microseconds(s)); }

// set 'false' if disable print
const bool print_msg = true;

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

}

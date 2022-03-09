#pragma once

#define _CRT_SECURE_NO_WARNINGS

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

inline void yield() { std::this_thread::yield(); }
inline void sleep_ms(u32 s) { std::this_thread::sleep_for(std::chrono::milliseconds(s)); }
inline void sleep_us(u32 s) { std::this_thread::sleep_for(std::chrono::microseconds(s)); }

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

class auto_memory
{
    struct internal_memory
    {
        internal_memory() :
            _ptr(nullptr),
            _size(0)
        {
        }

        ~internal_memory()
        {
            mem_free();
        }

        bool mem_alloc(u32 size)
        {
            mem_free();
            _ptr = malloc(size);
            if (_ptr)
            {
                _size = size;
            }
            return _ptr != nullptr;
        }

        void mem_free()
        {
            if (_ptr)
            {
                free(_ptr);
                _ptr = nullptr;
                _size = 0;
            }
        }

        void* _ptr;
        u32   _size;
    };

public:
    auto_memory() :
        _ref()
    {
    }

    auto_memory(u32 size)
    {
        auto r = ref<internal_memory>(::new internal_memory());
        if (r.get())
        {
            r->mem_alloc(size);
            if (r->_ptr)
            {
                _ref = r;
            }
        }
    }

    auto_memory(const auto_memory& rhs) :
        _ref(rhs._ref)
    {
    }

    auto_memory(auto_memory&& rhs) noexcept :
        _ref()
    {
        _ref.swap(rhs._ref);
    }

    auto_memory& operator =(const auto_memory& rhs)
    {
        _ref = rhs._ref;
        return *this;
    }

    auto_memory& operator =(auto_memory&& rhs) noexcept
    {
        _ref.swap(rhs._ref);
        return *this;
    }

    ~auto_memory() = default;

    operator bool()
    {
        return _ref.get() != nullptr;
    }

    bool operator ==(const auto_memory& rhs)
    {
        return _ref.get() == rhs._ref.get();
    }

    bool operator !=(const auto_memory& rhs)
    {
        return _ref.get() != rhs._ref.get();
    }

    template<typename Ty = void>
    Ty* get()
    {
        return _ref.get() ? reinterpret_cast<Ty*>(_ref->_ptr) : nullptr;
    }

    template<typename Ty = void>
    const Ty* get() const
    {
        return _ref.get() ? reinterpret_cast<Ty*>(_ref->_ptr) : nullptr;
    }

    u32 size() const
    {
        return _ref.get() ? _ref->_size : 0;
    }

private:
    ref<internal_memory> _ref;
};

class escape_function
{
    class internal_release_base
    {
    public:
        internal_release_base()
        {}

        virtual ~internal_release_base()
        {}

        virtual void disable() = 0;
    };

    template<typename Fn_Ty>
    class internal_release : public internal_release_base
    {
    public:
        internal_release(Fn_Ty fn) :
            _fn(fn),
            _is_active(true)
        {}

        virtual ~internal_release() override
        {
            if (_is_active)
            {
                _fn();
            }
        }

        virtual void disable() override
        {
            _is_active = false;
        }

    private:
        Fn_Ty _fn;
        bool  _is_active;
    };

public:
    escape_function() :
        _release(nullptr)
    {}

    template<typename Fn_Ty>
    escape_function(Fn_Ty fn) :
        _release(new internal_release<Fn_Ty>(fn))
    {}

    ~escape_function()
    {
        if (_release)
        {
            delete _release;
        }
    }

    template<typename Fn_Ty>
    escape_function& operator =(Fn_Ty fn)
    {
        if (_release)
        {
            _release->disable();
            delete _release;
        }
        _release = new internal_release<Fn_Ty>(fn);
        return *this;
    }

    void disable()
    {
        if (_release)
        {
            _release->disable();
            delete _release;
            _release = nullptr;
        }
    }

private:
    internal_release_base* _release;
};

}

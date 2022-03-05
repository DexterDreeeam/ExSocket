
#include<stdio.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "ex_udp.hpp"

int multi_thread = 8;

void report(long long cnt)
{
    std::cout << '\r';
    std::cout << cnt << " packets received.";
}

int main()
{
    auto start_time = std::chrono::system_clock::now();
    auto last_time = start_time;
    auto receiver = Eu::Receiver::Build(10086);
    std::atomic<long long> cnt = 0;
    std::vector<std::thread*> vt;
    volatile bool exit = false;

    for (int thread_i = 0; thread_i < multi_thread; ++thread_i)
    {
        auto* t = new std::thread(
            [&, thread_i]() mutable
            {
                char* buf = new char[64 * 1024 * 1024];
                while (!exit)
                {
                    unsigned long long len = receiver->Read(buf);
                    auto current_time = std::chrono::system_clock::now();
                    if (len > 0)
                    {
                        ++cnt;
                        if (current_time - last_time >= std::chrono::milliseconds(200))
                        {
                            last_time = current_time;
                            if (thread_i == 0)
                            {
                                report(cnt.load());
                            }
                        }
                    }
                    else if (current_time - last_time >= std::chrono::seconds(1))
                    {
                        last_time = current_time;
                        if (thread_i == 0)
                        {
                            report(cnt);
                        }
                    }
                }
                delete[] buf;
            });

        vt.push_back(t);
    }

    getchar();
    exit = true;
    for (auto* t : vt)
    {
        t->join();
        delete t;
    }
    return 0;
}


#include<stdio.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "ex_udp.hpp"

int max_ints = 2048;
int multi_thread = 8;

int main()
{
    auto start_time = std::chrono::system_clock::now();
    std::atomic<long long> cnt = 0;
    std::atomic<long long> send_size = 0;
    std::vector<std::thread*> vt;

    for (int thread_i = 0; thread_i < multi_thread; ++thread_i)
    {
        auto* t = new std::thread(
            [&]() mutable
            {
                auto sender = Eu::Sender::Build("127.0.0.1", 10086);
                char* buf = new char[max_ints * sizeof(int)];
                for (int i = 0; i < (1 << 13); ++i)
                {
                    int i_int = (rand() % max_ints) + 1;
                    int send_len = i_int * sizeof(int);
                    int* int_arr = (int*)buf;
                    for (int j = 0; j < i_int; ++j)
                    {
                        int_arr[j] = i_int;
                    }
                    if (sender->Send(buf, send_len))
                    {
                        ++cnt;
                        send_size += send_len;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                delete[] buf;
            });

        vt.push_back(t);
    }

    for (auto* t : vt)
    {
        t->join();
        delete t;
    }

    auto finish_time = std::chrono::system_clock::now();
    auto elapse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finish_time - start_time).count();
    auto m_bit_ps = (double)send_size * 8 / ((double)elapse_ms / 1000) / 1000000;
    std::cout << cnt << " packets sent." << std::endl;
    std::cout << "Transfer speed: " << (unsigned long long)m_bit_ps << " Mbps." << std::endl;

    return 0;
}

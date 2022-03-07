
#include <stdio.h>
#include <chrono>
#include <iostream>
#include <thread>

#include "ex_socket.hpp"

namespace udp_test
{

int max_ints = 1 << 22;
int sender_thread_cnt = 1;
int receiver_thread_cnt = 8;
int packets_cnt_per_thread = 1 << 11;

void receive_report(long long cnt)
{
    std::cout << '\r';
    std::cout << cnt << " packets received.";
    std::cout.flush();
}

void receive()
{
    auto start_time = std::chrono::system_clock::now();
    auto last_time = start_time;
    auto receiver = Es::Udp::Receiver::Build(10086);
    std::atomic<long long> cnt = 0;
    std::vector<std::thread*> vt;
    volatile bool exit = false;

    for (int thread_i = 0; thread_i < receiver_thread_cnt; ++thread_i)
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
                        if (thread_i == 0 && current_time - last_time >= std::chrono::milliseconds(200))
                        {
                            last_time = current_time;
                            receive_report(cnt.load());
                        }
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        if (thread_i == 0 && current_time - last_time >= std::chrono::milliseconds(400))
                        {
                            last_time = current_time;
                            receive_report(cnt);
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
    std::cout.flush();
}

void send()
{
    auto start_time = std::chrono::system_clock::now();
    std::atomic<long long> cnt = 0;
    std::atomic<long long> send_size = 0;
    std::vector<std::thread*> vt;

    std::cout << "Sending ..." << std::endl;

    for (int thread_i = 0; thread_i < sender_thread_cnt; ++thread_i)
    {
        auto* t = new std::thread(
            [&]() mutable
            {
                auto sender = Es::Udp::Sender::Build("127.0.0.1", 10086);
                char* buf = new char[max_ints * sizeof(int)];
                for (int i = 0; i < packets_cnt_per_thread; ++i)
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
                    std::this_thread::sleep_for(std::chrono::microseconds(2));
                }
                delete[] buf;
            });

        vt.push_back(t);
    }

    while (1)
    {
        int total_packet_cnt = packets_cnt_per_thread * sender_thread_cnt;
        int current_send_packet_cnt = (int)cnt.load();
        double completeness = (double)current_send_packet_cnt / (double)total_packet_cnt;
        std::cout << '\r';
        std::cout << "Completeness: " << int(completeness * 100) << '%';
        std::cout.flush();
        if (current_send_packet_cnt == total_packet_cnt)
        {
            std::cout << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
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
}

}

namespace tcp_test
{

void receive_report(long long cnt)
{
    std::cout << '\r';
    std::cout << cnt << " packets received.";
    std::cout.flush();
}

void receive()
{
    auto receiver = Es::Tcp::Receiver::Build(10086);

    while (1)
    {
        auto client = receiver->WaitClient();
        std::cout << "New client connected" << std::endl;

        int cnt = 0;
        char* buf = new char[1024 * 1024 * 64];
        while (receiver->Read(client, buf) > 0)
        {
            ++cnt;
            receive_report(cnt);
        }
        delete[] buf;

        std::cout << "New client disconnected" << std::endl;
        std::cout << "==================" << std::endl;
    }
}

void send()
{
    auto sender = Es::Tcp::Sender::Build("127.0.0.1", 10086);

    for (int i = 0; i < 1024; ++i)
    {
        int packet_sz = rand() % (1024 * 64);
        char* buf = new char[packet_sz];
        if (!sender->Send(buf, packet_sz))
        {
            std::cout << "Error happens!" << std::endl;
        }
        delete[] buf;
    }

    std::cout << "Press any key to exit" << std::endl;
    getchar();
}

}

int main()
{
    std::cout << "1. <Udp> Send data" << std::endl;
    std::cout << "2. <Udp> Receive data" << std::endl;
    std::cout << "3. [Tcp] Send data" << std::endl;
    std::cout << "4. [Tcp] Receive data" << std::endl;
    std::cout << ">>>>>>>>> Select: ";
    int option;
    std::cin >> option;
    getchar();
    if (option == 1)
    {
        udp_test::send();
    }
    else if (option == 2)
    {
        udp_test::receive();
    }
    else if (option == 3)
    {
        tcp_test::send();
    }
    else if (option == 4)
    {
        tcp_test::receive();
    }
    else
    {
        return 1;
    }
    return 0;
}

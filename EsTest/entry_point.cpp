#include "../ExSocket/es.hpp"

const int tcp_test_count = 1 << 14;

void tcp_send()
{
    auto sender = es::tcp::sender::build("127.0.0.1", 10086);

    int i = 1;
    while (i <= tcp_test_count)
    {
        char* buf = new char[i];
        sender->send(buf, i);
        delete[] buf;
        ++i;
    }
    std::cout << i - 1 << " tcp packets sent." << std::endl;
    std::cout << "press any key to exit." << std::endl;
    getchar();
    getchar();
}

void tcp_recv()
{
    class my_notifier : public es::tcp::notifier
    {
    public:
        virtual void on_client_connect(long long client) override
        {
            std::cout << "Client " << client << " connected." << std::endl;
        }

        virtual void on_client_disconnect(long long client) override
        {
            std::cout << "Client " << client << " disconnected." << std::endl;
        }

        virtual void on_message_arrive(long long client, const void* msg, long long msg_len) override
        {
            static int msg_cnt = 0;
            if (msg_cnt == 0)
            {
                _tp = std::chrono::system_clock::now();
            }

            ++msg_cnt;
            if (msg_cnt == msg_len)
            {
                std::cout << "\r" << std::flush << msg_cnt << " tcp packetse received.";
            }
            else
            {
                std::cout << std::endl << "Error." << std::endl;
            }

            if (msg_cnt == tcp_test_count)
            {
                auto diff_time = std::chrono::system_clock::now() - _tp;
                std::cout << std::endl;
                std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(diff_time).count() << "ms elapse." << std::endl;
            }
        }

        std::chrono::system_clock::time_point _tp;
    };

    auto recver = es::tcp::receiver::build(10086, new my_notifier());
    getchar();
    getchar();
}

void tcp_recv_direct()
{
    WSADATA wsa_data = {};
    ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    char host_name[256] = {};
    ::gethostname(host_name, sizeof(host_name));
    auto sk = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
    addr.sin_port = ::htons((u_short)10086);
    ::bind(sk, (SOCKADDR*)&addr, sizeof(SOCKADDR));
    int sk_buf_len = 64 * 1024 * 1024;
    ::setsockopt(sk, SOL_SOCKET, SO_RCVBUF, (char*)&sk_buf_len, sizeof(sk_buf_len));
    ::listen(sk, SOMAXCONN);
    int addr_len = sizeof(addr);
    auto client_sk = ::accept(sk, (sockaddr*)&addr, &addr_len);

    decltype(std::chrono::system_clock::now()) start_time;
    int expect = 1;
    while (expect <= tcp_test_count)
    {
        long long read_len;
        char* buf = new char[1024 * 1024 * 8];
        recv(client_sk, buf, 8, 0);
        read_len = ((int*)buf)[1];
        read_len = recv(client_sk, buf, (int)read_len, 0);
        if (expect == 1)
        {
            start_time = std::chrono::system_clock::now();
        }
        if (read_len != expect)
        {
            std::cout << "ERROR!" << std::endl;
            delete[] buf;
            break;
        }
        std::cout << "\r" << std::flush;
        std::cout << expect << " tcp packetse received.";
        ++expect;
        delete[] buf;
    }
    auto diff_time = std::chrono::system_clock::now() - start_time;
    std::cout << std::endl;
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(diff_time).count() << "ms elapse." << std::endl;
    std::cout << "press any key to exit." << std::endl;
}

int main()
{
    std::cout << "1. [TCP] Send -->> Test" << std::endl;
    std::cout << "2. [TCP] Recv <<-- Test" << std::endl;
    std::cout << "3. [TCP] Recv[d] <<-- Test" << std::endl;
    std::cout << "4. <UDP> Send -->> Test" << std::endl;
    std::cout << "5. <UDP> Recv <<-- Test" << std::endl;
    std::cout << ">>>>>>>>> Select: ";
    int opt;
    std::cin >> opt;

    if (opt == 1)
    {
        tcp_send();
    }
    else if (opt == 2)
    {
        tcp_recv();
    }
    else if (opt == 3)
    {
        tcp_recv_direct();
    }
    return 0;
}

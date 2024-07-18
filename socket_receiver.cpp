#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <atomic>
#include <sys/socket.h>
#include <sys/types.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <csignal>
#include <chrono>
#include <thread>

#define BUF_SIZE 1024

struct Stats {
    std::atomic<uint64_t> total_packets{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> bytes_second{0};
    std::atomic<uint64_t> packets_second{0};
    std::chrono::steady_clock::time_point start_time;
};

static struct Stats global_stats;
static std::atomic<bool> force_quit{false};

std::string format_unit(double value) {
    const std::array<std::string, 5> units = {"", "K", "M", "G", "T"};
    int i = 0;
    while (value >= 1000.0 && i < 4) {
        value /= 1000.0;
        i++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value << " " << units[i];
    return oss.str();
}

void print_stats() {
    std::cout << "\rStats: " 
              << format_unit(global_stats.total_packets.load()) << "-packets, "
              << format_unit(global_stats.total_bytes.load()) << "bytes, "
              << format_unit(global_stats.packets_second.load()) << "-packets/s, "
              << format_unit(global_stats.bytes_second.load()) << "b/s   " << std::flush;
    
    global_stats.packets_second = 0;
    global_stats.bytes_second = 0;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\nSignal " << signum << " received, preparing to exit..." << std::endl;
        force_quit = true;
    }
}

void stats_thread() {
    while (!force_quit) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        print_stats();
    }
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int sockfd;
    struct sockaddr_ll socket_address;
    uint8_t buffer[BUF_SIZE + sizeof(struct ether_header)];

    // Создание сокета
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&socket_address, 0, sizeof(socket_address));

    // Установка интерфейса
    const char* interface = "enp0s9";  // Замените на имя вашего интерфейса
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = if_nametoindex(interface);
    socket_address.sll_protocol = htons(ETH_P_ALL);

    if (socket_address.sll_ifindex == 0) {
        perror("if_nametoindex failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Привязка сокета к интерфейсу
    if (bind(sockfd, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    global_stats.start_time = std::chrono::steady_clock::now();

    std::thread stats(stats_thread);

    // Сбор статистики
    while (!force_quit) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }

        global_stats.total_packets++;
        global_stats.total_bytes += n;
        global_stats.packets_second++;
        global_stats.bytes_second += n;
    }

    stats.join();

    std::cout << std::endl;
    std::cout << "Receiver stopped by user." << std::endl;

    std::cout << "Total messages: " << global_stats.total_packets << std::endl;
    std::cout << "Total bytes: " << global_stats.total_bytes << " bytes" << std::endl;

    close(sockfd);
    return 0;
}

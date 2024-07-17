#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
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

#define BUF_SIZE 1024

struct stats {
    uint64_t total_packets;
    uint64_t total_bytes;
    uint64_t bytes_second;
    uint64_t packets_second;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};

static struct stats global_stats = {0, 0, 0, 0, std::chrono::high_resolution_clock::now()};

static const char *format_unit(double *value, const char **unit) {
    const char *units[] = {"", "K", "M", "G", "T"};
    int i = 0;
    while (*value >= 1000.0 && i < 4) {
        *value /= 1000.0;
        i++;
    }
    *unit = units[i];
    return *unit;
}

std::string print_stats(void) {
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - global_stats.start_time;
    double duration = elapsed.count();

    double packets_per_sec = global_stats.packets_second / duration;
    double bytes_per_sec = global_stats.bytes_second / duration;

    const char *packet_unit, *byte_unit, *pps_unit, *bps_unit;
    double formatted_packets = global_stats.total_packets;
    double formatted_bytes = global_stats.total_bytes;
    double formatted_pps = packets_per_sec;
    double formatted_bps = bytes_per_sec;

    format_unit(&formatted_packets, &packet_unit);
    format_unit(&formatted_bytes, &byte_unit);
    format_unit(&formatted_pps, &pps_unit);
    format_unit(&formatted_bps, &bps_unit);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "Stats: " << formatted_packets << " " << packet_unit << "-packets, "
        << formatted_bytes << " " << byte_unit << "bytes, "
        << formatted_pps << " " << pps_unit << "-packets/s, "
        << formatted_bps << " " << bps_unit << "b/s";
    
    return oss.str();
}

volatile sig_atomic_t stop;
void handle_interrupt(int /*signum*/)
{
    stop = 1;
}

int main() {
    signal(SIGINT, handle_interrupt);

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

    global_stats.start_time = std::chrono::high_resolution_clock::now();

    // Сбор статистики
    while (!stop) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }

        global_stats.total_packets++;
        global_stats.total_bytes += n;
        global_stats.packets_second++;
        global_stats.bytes_second += n;

        // Периодически выводить статистику
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - global_stats.start_time;
        if (elapsed.count() >= 1.0) {
            std::string stats = print_stats();
            std::cout << "\r" << stats << std::flush;

            global_stats.start_time = now; // Сброс таймера
            global_stats.packets_second = 0; // Сброс счетчика пакетов
            global_stats.bytes_second = 0; // Сброс счетчика байт
        }
    }

    std::cout << std::endl;
    std::cout << "Receiver stopped by user." << std::endl;

    std::cout << "Total messages: " << global_stats.total_packets << std::endl;
    std::cout << "Total bytes: " << global_stats.total_bytes << " bytes" << std::endl;

    close(sockfd);
    return 0;
}

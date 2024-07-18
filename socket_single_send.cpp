#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <chrono>

#define THREAD_COUNT 4

static bool use_sleep = true;

void get_mac_address(const char* ifname, uint8_t* mac) {
    struct ifaddrs *ifap, *ifa;
    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_PACKET && strcmp(ifa->ifa_name, ifname) == 0) {
            struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
            memcpy(mac, s->sll_addr, 6);
            break;
        }
    }
    freeifaddrs(ifap);
}

struct stats {
    uint64_t total_packets;
    uint64_t total_bytes;
    std::atomic<uint64_t> bytes_second;
    std::atomic<uint64_t> packets_second;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

    stats() : total_packets(0), total_bytes(0), bytes_second(0), packets_second(0), start_time(std::chrono::high_resolution_clock::now()) {}
};

static stats global_stats;

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
    double packets_per_sec = global_stats.packets_second;
    double bytes_per_sec = global_stats.bytes_second;

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
        << formatted_bps << " " << bps_unit << "b/s   ";
    
    return oss.str();
}

volatile sig_atomic_t stop;
void handle_interrupt(int /*signum*/) {
    stop = 1;
}

void send_packets(const char* interface, const uint8_t* dst_mac, int thread_id, int buf_size) {
    int sockfd;
    struct sockaddr_ll socket_address;
    char* buffer = new char[buf_size + 1];
    std::memset(buffer, 'A', buf_size);
    buffer[buf_size] = '\0';

    // Создание сокета
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        perror("socket creation failed");
        delete[] buffer;
        return;
    }

    memset(&socket_address, 0, sizeof(socket_address));

    // Установка интерфейса
    socket_address.sll_ifindex = if_nametoindex(interface);
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, dst_mac, 6);

    // MAC-адрес источника
    uint8_t src_mac[6];
    get_mac_address(interface, src_mac);

    // Создание Ethernet кадра
    std::vector<uint8_t> frame(buf_size + sizeof(struct ether_header));
    struct ether_header *eh = reinterpret_cast<struct ether_header *>(frame.data());
    memcpy(eh->ether_shost, src_mac, 6);
    memcpy(eh->ether_dhost, dst_mac, 6);
    eh->ether_type = htons(ETH_P_IP);

    // Добавление полезной нагрузки
    memcpy(frame.data() + sizeof(struct ether_header), buffer, buf_size);

    // Отправка сообщений
    while (!stop) {
        if (sendto(sockfd, frame.data(), frame.size(), 0, reinterpret_cast<struct sockaddr*>(&socket_address), sizeof(socket_address)) < 0) {
            perror("sendto failed");
            break;
        } else {
            global_stats.total_packets++;
            global_stats.total_bytes += buf_size + sizeof(struct ether_header);
            global_stats.packets_second++;
            global_stats.bytes_second += buf_size + sizeof(struct ether_header);
        }
        if(use_sleep) {
            usleep(1000);  // Пауза для демонстрации
        }
    }

    close(sockfd);
    delete[] buffer;
    std::cout << "Thread " << thread_id << " stopped." << std::endl;
}

void stats_thread() {
    while (!stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::string stats = print_stats();
        std::cout << "\r" << stats << std::flush;
        global_stats.packets_second = 0;
        global_stats.bytes_second = 0;
    }
}

void parse_mac_address(const std::string &mac_str, uint8_t mac[6]) {
    std::stringstream ss(mac_str);
    std::string byte_str;
    int i = 0;

    while (std::getline(ss, byte_str, ':') && i < 6) {
        mac[i++] = std::stoi(byte_str, nullptr, 16);
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_interrupt);

    int buf_size = 1024;
    uint8_t dst_mac[6] = {0x08, 0x00, 0x27, 0x60, 0xff, 0x20};

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            buf_size = std::stoi(argv[++i]);
        }
        if (arg == "--no-sleep") {
            use_sleep = false;
        }
        if (arg == "--dst" && i + 1 < argc) {
            parse_mac_address(argv[++i], dst_mac);
        }
    }

    const char* interface = "enp0s9";
    global_stats.start_time = std::chrono::high_resolution_clock::now();

    std::thread stats_thread_handle(stats_thread);

    // Запуск потоков
    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back(send_packets, interface, dst_mac, i, buf_size);
    }

    // Ожидание завершения всех потоков
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    stats_thread_handle.join();
    std::cout << std::endl;
    std::cout << "Sender stopped by user." << std::endl;

    std::cout << "Total messages: " << global_stats.total_packets << std::endl;
    std::cout << "Total bytes: " << global_stats.total_bytes << " bytes" << std::endl;

    return 0;
}
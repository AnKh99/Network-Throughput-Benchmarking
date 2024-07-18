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

static int thread_count = 4;
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
              << format_unit(global_stats.bytes_second.load()) << "b/s" << std::flush;
    
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
    while (!force_quit) {
        if (sendto(sockfd, frame.data(), frame.size(), 0, reinterpret_cast<struct sockaddr*>(&socket_address), sizeof(socket_address)) < 0) {
            perror("sendto failed");
            break;
        } else {
            global_stats.total_packets++;
            global_stats.total_bytes += buf_size + sizeof(struct ether_header);
            global_stats.packets_second++;
            global_stats.bytes_second += buf_size + sizeof(struct ether_header);
        }
        if (use_sleep) {
            usleep(1000);  // Пауза для демонстрации
        }
    }

    close(sockfd);
    delete[] buffer;
    std::cout << "Thread " << thread_id << " stopped." << std::endl;
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
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

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
        if (arg == "--j" && i + 1 < argc) {
            thread_count = std::stoi(argv[++i]);
        }
    }

    const char* interface = "enp0s9";

    std::thread stats(stats_thread);

    // Запуск потоков
    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(send_packets, interface, dst_mac, i, buf_size);
    }

    // Ожидание завершения всех потоков
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    stats.join();
    std::cout << std::endl;
    std::cout << "Sender stopped by user." << std::endl;

    std::cout << "Total messages: " << global_stats.total_packets << std::endl;
    std::cout << "Total bytes: " << global_stats.total_bytes << " bytes" << std::endl;

    return 0;
}
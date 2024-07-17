#include <iostream>
#include <iomanip>
#include <cstring>
#include <string> 
#include <thread>
#include <atomic>
#include <csignal>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <chrono>
#include <array>

constexpr uint16_t RX_RING_SIZE = 1024;
constexpr uint16_t TX_RING_SIZE = 1024;
constexpr uint16_t NUM_MBUFS = 2048;
constexpr uint16_t MBUF_CACHE_SIZE = 128;
constexpr uint16_t BURST_SIZE = 32;
static uint16_t message_size = 128;
static bool use_sleep = true;

struct stats {
    std::atomic<uint64_t> total_packets;
    std::atomic<uint64_t> total_bytes;
    std::atomic<uint64_t> bytes_second;
    std::atomic<uint64_t> packets_second;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;

    stats() : total_packets(0), total_bytes(0), bytes_second(0), packets_second(0), start_time(std::chrono::high_resolution_clock::now()) {}
};

static stats global_stats;
static volatile sig_atomic_t stop;

void handle_interrupt(int /*signum*/) {
    stop = 1;
}

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
        << formatted_bps << " " << bps_unit << "b/s";
    
    return oss.str();
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

int port_init(uint16_t port, rte_mempool* mbuf_pool) {
    struct rte_eth_conf port_conf_default = {};
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;

    if (!rte_eth_dev_is_valid_port(port)) return -1;

    struct rte_eth_dev_info dev_info;
    int retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) return retval;

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf_default);
    if (retval != 0) return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) return retval;

    struct rte_eth_rxconf rxconf = dev_info.default_rxconf;
    rxconf.offloads = port_conf_default.rxmode.offloads;
    for (uint16_t q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), &rxconf, mbuf_pool);
        if (retval < 0) return retval;
    }

    struct rte_eth_txconf txconf = dev_info.default_txconf;
    txconf.offloads = port_conf_default.txmode.offloads;
    for (uint16_t q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), &txconf);
        if (retval < 0) return retval;
    }

    retval = rte_eth_dev_start(port);
    if (retval < 0) return retval;

    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval < 0) return retval;

    std::cout << "Port " << port << " MAC: "
              << std::hex << std::setw(2) << std::setfill('0')
              << (int)addr.addr_bytes[0] << ":"
              << (int)addr.addr_bytes[1] << ":"
              << (int)addr.addr_bytes[2] << ":"
              << (int)addr.addr_bytes[3] << ":"
              << (int)addr.addr_bytes[4] << ":"
              << (int)addr.addr_bytes[5] << std::dec << std::endl;

    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0) return retval;

    return 0;
}

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            message_size = std::stoi(argv[++i]);
        }
        if (arg == "--no-sleep") {
            use_sleep = false;
        }
    }

    constexpr uint16_t portid = 0;
    
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == nullptr) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    if (port_init(portid, mbuf_pool) != 0) rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", portid);

    rte_ether_addr dst_mac;
    rte_ether_addr src_mac;

    const char* mac_str = "08:00:27:56:59:dd";
    sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &dst_mac.addr_bytes[0], &dst_mac.addr_bytes[1], &dst_mac.addr_bytes[2],
        &dst_mac.addr_bytes[3], &dst_mac.addr_bytes[4], &dst_mac.addr_bytes[5]);

    rte_eth_macaddr_get(portid, &src_mac);

    std::signal(SIGINT, handle_interrupt);
    std::signal(SIGTERM, handle_interrupt);

    std::thread stats_thread_handle(stats_thread);

    while (!stop) {
        std::array<rte_mbuf*, BURST_SIZE> bufs;
        
        for (auto& buf : bufs) {
            buf = rte_pktmbuf_alloc(mbuf_pool);
            if (buf == nullptr) {
                rte_exit(EXIT_FAILURE, "Failed to allocate mbuf\n");
            }
            auto *packet_data = rte_pktmbuf_mtod(buf, rte_ether_hdr*);
            rte_ether_addr_copy(&dst_mac, &packet_data->dst_addr);
            rte_ether_addr_copy(&src_mac, &packet_data->src_addr);
            packet_data->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

            auto payload = reinterpret_cast<char*>(packet_data + 1);
            std::memset(payload, 'A', message_size - sizeof(rte_ether_hdr));

            buf->data_len = message_size;
            buf->pkt_len = message_size;
        }

        uint16_t nb_tx = rte_eth_tx_burst(portid, 0, bufs.data(), BURST_SIZE);
        if (nb_tx) {
            global_stats.total_packets += nb_tx;
            global_stats.total_bytes += nb_tx * message_size;
            global_stats.packets_second += nb_tx;
            global_stats.bytes_second += nb_tx * message_size;

            for (uint16_t buf = nb_tx; buf < BURST_SIZE; buf++)
                rte_pktmbuf_free(bufs[buf]);
        }

        if (use_sleep) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Simulate processing time
        }
    }

    stats_thread_handle.join();
    std::cout << std::endl;
    std::cout << "Sender stopped by user." << std::endl;

    return 0;
}
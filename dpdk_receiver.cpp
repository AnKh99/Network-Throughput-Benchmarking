#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <array>
#include <memory>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

constexpr uint16_t RX_RING_SIZE = 1024;
constexpr uint16_t TX_RING_SIZE = 1024;
constexpr uint16_t NUM_MBUFS = 8191;
constexpr uint16_t MBUF_CACHE_SIZE = 250;
constexpr uint16_t BURST_SIZE = 32;

struct Stats {
    std::atomic<uint64_t> total_packets{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> bytes_second{0};
    std::atomic<uint64_t> packets_second{0};
    std::chrono::steady_clock::time_point start_time;
};

static Stats global_stats;
static std::atomic<bool> force_quit{false};
static bool use_sleep = true;

static const struct rte_eth_conf port_conf_default = {
    .link_speeds = 0,
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .mtu = 1500,
        .max_lro_pkt_size = 0,
        .offloads = 0,
        .reserved_64s = {0},
        .reserved_ptrs = {nullptr}
    },
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
        .offloads = 0,
        .pvid = 0,
        .hw_vlan_reject_tagged = 0,
        .hw_vlan_reject_untagged = 0,
        .hw_vlan_insert_pvid = 0,
        .reserved_64s = {0},
        .reserved_ptrs = {nullptr}
    },
    .lpbk_mode = 0,
    .rx_adv_conf = {},
    .tx_adv_conf = {},
    .dcb_capability_en = 0,
    .intr_conf = {}
};

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
    double packets_per_sec = global_stats.packets_second;
    double bytes_per_sec = global_stats.bytes_second;

    global_stats.packets_second = 0;
    global_stats.bytes_second = 0;

    std::cout << "\rStats: " 
              << format_unit(global_stats.total_packets.load()) << "-packets, "
              << format_unit(global_stats.total_bytes.load()) << "bytes, "
              << format_unit(packets_per_sec) << "-packets/s, "
              << format_unit(bytes_per_sec) << "b/s" << std::flush;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\nSignal " << signum << " received, preparing to exit..." << std::endl;
        force_quit = true;
    }
}

int port_init(uint16_t port, rte_mempool* mbuf_pool) {
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = 0;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    rte_eth_dev_info dev_info;
    int retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        std::cerr << "Error during getting device (port " << port << ") info: " << strerror(-retval) << std::endl;
        return retval;
    }

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    rte_eth_rxconf rxconf = dev_info.default_rxconf;
    rxconf.offloads = port_conf.rxmode.offloads;
    for (uint16_t q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                rte_eth_dev_socket_id(port), &rxconf, mbuf_pool);
        if (retval < 0)
            return retval;
    }

    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    return 0;
}

void receive_packets(uint16_t portid) {
    while (!force_quit) {
        std::array<rte_mbuf*, BURST_SIZE> bufs;
        uint16_t nb_rx = rte_eth_rx_burst(portid, 0, bufs.data(), BURST_SIZE);

        if (nb_rx > 0) {
            global_stats.total_packets += nb_rx;
            global_stats.packets_second += nb_rx;
            for (int i = 0; i < nb_rx; i++) {
                global_stats.total_bytes += bufs[i]->pkt_len;
                global_stats.bytes_second += bufs[i]->pkt_len;
                rte_pktmbuf_free(bufs[i]);
            }
        }

        if (use_sleep) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Небольшая пауза для снижения нагрузки на CPU
        }
    }
}

void stats_thread() {
    global_stats.start_time = std::chrono::steady_clock::now();
    auto last_print_time = global_stats.start_time;

    while (!force_quit) {
        auto current_time = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - last_print_time).count() >= 1) {
            print_stats();
            last_print_time = current_time;
        }
    }
}

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-sleep") {
            use_sleep = false;
        }
    }

    auto mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == nullptr)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    constexpr uint16_t portid = 0;
    if (port_init(portid, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", portid);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "Receiving packets. Press Ctrl+C to exit...\n";

    std::thread receiver(receive_packets, portid);
    std::thread stats(stats_thread);

    receiver.join();
    stats.join();

    std::cout << "\nReceiver stopped." << std::endl;

    return 0;
}

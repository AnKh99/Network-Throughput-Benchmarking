#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>

int main(int argc, char *argv[])
{
    int ret;
    uint16_t port_id = 0;  // Укажите ваш порт

    // Инициализация EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    }

    // Проверка, что порт валиден
    if (!rte_eth_dev_is_valid_port(port_id)) {
        rte_exit(EXIT_FAILURE, "Invalid port\n");
    }

    // Получение MAC-адреса
    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(port_id, &mac_addr);

    // Печать MAC-адреса
    printf("Port %u MAC: %02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 "\n",
           port_id,
           mac_addr.addr_bytes[0],
           mac_addr.addr_bytes[1],
           mac_addr.addr_bytes[2],
           mac_addr.addr_bytes[3],
           mac_addr.addr_bytes[4],
           mac_addr.addr_bytes[5]);

    return 0;
}
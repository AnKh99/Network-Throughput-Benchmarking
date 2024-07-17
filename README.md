# Network Throughput Benchmarking

## Описание проекта

Этот проект направлен на тестирование пропускной способности сетевых интерфейсов между двумя виртуальными машинами (VM1 - отправитель и VM2 - получатель) на базе Ubuntu с использованием сокетов, многопоточности и DPDK.

## Файлы в проекте

- `benchmark.py`: Скрипт на Python для запуска различных режимов тестирования на обеих машинах и сбора статистики с результатами.
- `dpdk_receiver.cpp`: Программа на C++ для приема сообщений с использованием DPDK и сбора статистики.
- `get_mac.cpp`: Программа на C++ для получения MAC-адреса сетевого интерфейса с использованием DPDK.
- `socket_single_send.cpp`: Программа на C++ для отправки сообщений с использованием сокетов и сбора статистики.
- `socket_mt_send`: Программа на C++ для отправки сообщений с использованием сокетов и многопоточности.
- `socket_receiver`: Программа на C++ для приема сообщений с использованием сокетов и сбора статистики.

## Требования

- VirtualBox
- Ubuntu 24.04 LTS на виртуальных машинах
- Установленные пакеты: `build-essential`, `meson`, `ninja-build`, `python3-pyelftools`, `libnuma-dev`, `python3-paramiko`, `clang-tidy`
- Установленный DPDK 23.11
- Обе виртуальные машины должны быть настроены с достаточным количеством памяти (используется 8 ГБ) и CPU (используется 6 ядер CPU).

## Инструкции по сборке и запуску

### Настройка виртуальных машин

1. Установите необходимые пакеты на обеих виртуальных машинах:
    ```sh
    sudo apt update && sudo apt upgrade -y
    sudo apt install -y build-essential meson ninja-build python3-pyelftools libnuma-dev python3-paramiko clang-tidy
    ```

2. Установите dpdk на обеих виртуальных машинах:
    ```sh
    wget https://fast.dpdk.org/rel/dpdk-23.11.tar.xz
    tar xJf dpdk-23.11.tar.xz
    cd dpdk-23.11
    meson build
    cd build
    ninja
    sudo ninja install
    sudo ldconfig
    ```
3. Сконфигурируйте параметры сети на обеих виртуальных машинах:
   ```sh
   pkg-config --modversion libdpdk
 
   echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
   sudo mkdir -p /mnt/huge
   sudo mount -t hugetlbfs nodev /mnt/huge
   ```

   Измените `/etc/fstab`, добавив в конец файла `nodev /mnt/huge hugetlbfs defaults 0 0`
   
   Свяжите dpdk с интерфейсом адаптера
   ```sh
   sudo modprobe uio
   sudo modprobe uio_pci_generic
   
   sudo ip link set enp0s8 down
   sudo ip addr flush dev enp0s8
   sudo dpdk-devbind.py -u 0000:00:08.0
   sudo dpdk-devbind.py --bind=uio_pci_generic 0000:00:08.0
   dpdk-devbind.py --status-dev net
   ls /dev/uio* 
   sudo chmod 666 /dev/uio0
   ```

4. Настройте SSH на обеих машинах.
   ```sh
   sudo apt install openssh-server
   ```

P.S. Данные для подключения к виртуальным машинам с помощью `benchmark.py`
```py
'vm1': {
    'host': 'localhost',
    'port': 2222,
    'username': 'vm1',
    'password': '1',
},
'vm2': {
    'host': 'localhost',
    'port': 2223,
    'username': 'vm2',
    'password': '1',
}
```

### Сборка программ

1. Создайте файл `CMakeLists.txt` в корневой директории проекта:

    ```cmake
    cmake_minimum_required(VERSION 3.10)
    project(DPDKTest)

    set(CMAKE_CXX_STANDARD 20)

    # Add the required flag for SSSE3
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3")

    # Enable Clang-Tidy
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        find_program(CLANG_TIDY_EXE NAMES "clang-tidy" REQUIRED)
        if(CLANG_TIDY_EXE)
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        endif()
    endif()

    # Find DPDK
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)

    # Include DPDK directories
    include_directories(${DPDK_INCLUDE_DIRS})

    # Link DPDK libraries
    link_directories(${DPDK_LIBRARY_DIRS})

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

    add_executable(get_mac get_mac.cpp)
    add_executable(socket_single_send socket_single_send.cpp)
    add_executable(socket_mt_send socket_mt_send.cpp)
    add_executable(dpdk_receiver dpdk_receiver.cpp)
    add_executable(dpdk_sender dpdk_sender.cpp)
    add_executable(socket_receiver socket_receiver.cpp)

    target_link_libraries(get_mac ${DPDK_LIBRARIES})
    target_link_libraries(dpdk_receiver ${DPDK_LIBRARIES})
    target_link_libraries(dpdk_sender ${DPDK_LIBRARIES})
    ```

2. Соберите программы:
    ```sh
    mkdir build
    cd build
    cmake ..
    make
    ```

### Запуск тестов
1. Запустите скрипт `benchmark.py` на локальной машине:
    ```sh
    python3 benchmark.py
    ```

2. Следуйте инструкциям в скрипте для инициализации среды DPDK и запуска тестов.

### Запуск утилит
1. Запуск `dpdk_receiver`:
    - `--no-sleep` - optional - отключает функцию `sleep`
    ```sh
    sudo ./dpdk_receiver
    ```
2. Запуск `dpdk_sender`:
    - `--no-sleep` - optional - отключает функцию `sleep`
    - `--size N` - optional - задает размер отправляемых пакетов. По умолчанию `size=128`
    ```sh
    sudo ./dpdk_sender -l 0-3 -n 4 -- -p 0x1
    ```
3. Запуск `socket_receiver`:
    `--no-sleep` - optional - отключает функцию `sleep`
    ```sh
    sudo ./socket_recevier
    ```
3. Запуск `socket_sender`:
    `--no-sleep` - optional - отключает функцию `sleep`
    - `--size N` - optional - задает размер отправляемых пакетов. По умолчанию `size=128`
    ```sh
    sudo ./socket_sender
    ```
3. Запуск `socket_mt_send`:
    - `--no-sleep` - optional - отключает функцию `sleep`
    - `--size N` - optional - задает размер отправляемых пакетов. По умолчанию `size=1024`
    ```sh
    sudo ./socket_mt_send
    ```

## Результаты

Результаты тестов будут отображены в консоли. Скрипт `benchmark.py` также собирает статистику и отображает её на экран.


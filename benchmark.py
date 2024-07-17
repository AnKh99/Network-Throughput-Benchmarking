import paramiko
import time

# Параметры подключения
HOSTS = {
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
}

# Удаленное выполнение команды через SSH с использованием одного соединения
def ssh_execute_commands(host_info, commands, stream_output=False):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host_info['host'], port=host_info['port'], username=host_info['username'], password=host_info['password'])
    
    channel = client.invoke_shell()
    channel.send(f"echo '{host_info['password']}' | sudo -S -i\n")
    time.sleep(1)  # Ждем приглашение sudo
    
    for command in commands:
        print(f"Executing: {command}")
        channel.send(f"{command}\n")
        time.sleep(1)  # Ждем выполнения команды
        
    if stream_output:
        while True:
            if channel.recv_ready():
                output = channel.recv(1024).decode('utf-8')
                print(output, end='')
            else:
                time.sleep(0.1)
    else:
        output = ""
        while channel.recv_ready():
            output += channel.recv(1024).decode('utf-8')
        print(output)
    
    channel.close()
    client.close()

# Инициализация виртуальных машин для DPDK
def initialize_dpdk():
    commands = [
        'pkg-config --modversion libdpdk',
        'echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages',
        'mkdir -p /mnt/huge',
        'sudo mount -t hugetlbfs nodev /mnt/huge',
        'sudo modprobe uio',
        'sudo modprobe uio_pci_generic',
        'ip link show',
        'sudo dpdk-devbind.py -u 0000:00:08.0',
        'sudo dpdk-devbind.py --bind=uio_pci_generic 0000:00:08.0',
        'sudo dpdk-devbind.py --status-dev net',
        'ls /dev/uio*',
        'sudo chmod 666 /dev/uio0'
    ]
    
    for vm in HOSTS:
        ssh_execute_commands(HOSTS[vm], commands)
    
    print("Initial setup done")

# Функция для запуска тестов
def run_tests():
    package_size = input("Set packet size(for sender only): ")

    print("\n1. Run Single Thread Sender")
    print("2. Run Multi Thread Sender")
    print("3. Run DPDK Sender")
    print("4. Run DPDK Receiver")
    print("5. Run Socket Receiver")
    choice = input("Select an option: ")

    commands = {
        "1" : [
                'cd ~/dpdk',
                'sudo ./socket_single --size ' + package_size
        ],
        "2" : [
                'cd ~/dpdk',
                'sudo ./socket_mt_send --size ' + package_size
        ],
        "3" : [
                'cd ~/dpdk',
                'sudo ./dpdk_sender -l 0-3 -n 4 -- -p 0x1 --size ' + package_size
        ],
        "4" : [
            'cd ~/dkdp',
            "sudo ./dpdk_receiver"
        ],
        "5" : [
            'cd ~/dpdk',
            "sudo ./socket_receiver"
        ]
    }

    if choice in ['1', '2', '3']:
        ssh_execute_commands(HOSTS['vm1'], commands[choice], stream_output=True)
    elif choice in ['4', '5']:
        ssh_execute_commands(HOSTS['vm2'], commands[choice], stream_output=True)
    else:
        print("Invalid choice")

    return

def main():
    print("1. Initialize DPDK environment")
    print("2. Run benchmarks")
    choice = input("Select an option: ")

    if choice == '1':
        max_retries = 3
        for attempt in range(max_retries):
            try:
                initialize_dpdk()
                break
            except Exception as e:
                print(f"Attempt {attempt + 1} failed: {str(e)}")
                if attempt == max_retries - 1:
                    print("Max retries reached. Initialization failed.")
                else:
                    print("Retrying...")
    elif choice == '2':
        run_tests()
    else:
        print("Invalid choice")

if __name__ == "__main__":
    main()

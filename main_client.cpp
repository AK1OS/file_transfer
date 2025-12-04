#include "interactive_tcp_client.h"
#include "network_utils.h"
#include <iostream>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "       文件传输客户端" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::string serverIP;
    int serverPort;
    
    if (!NetworkUtils::discoverServer(serverIP, serverPort)) {
        std::cerr << " 无法发现服务器，程序退出" << std::endl;
        return 1;
    }
    
    try {
        InteractiveTCPClient client(serverIP, serverPort);
        client.runInteractive();
    } catch (const std::exception& e) {
        std::cerr << " 程序错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
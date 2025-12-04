#include "network_utils.h"
#include "../common/constants.h"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <ctime>
#include <sys/time.h>
#include <ifaddrs.h>
#include <cstring>
#include <iomanip>

int NetworkUtils::createConnection(const std::string& serverIP, int serverPort) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Socket creation failed");
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Invalid address");
    }

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        throw std::runtime_error("Connection failed");
    }

    return sock;
}

bool NetworkUtils::discoverServer(std::string& discoveredIP, int& discoveredPort) {
    std::cout << " 正在搜索服务器..." << std::endl;

    int discoverySocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discoverySocket < 0) {
        std::cerr << "创建发现套接字失败" << std::endl;
        return false;
    }

    // 设置广播选项
    int broadcastEnable = 1;
    if (setsockopt(discoverySocket, SOL_SOCKET, SO_BROADCAST, 
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        std::cerr << "设置广播选项失败" << std::endl;
        close(discoverySocket);
        return false;
    }

    // 设置接收超时
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(discoverySocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // 发送广播发现请求到所有网络接口
    struct sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    const char* discoveryMsg = "FILE_SERVER_DISCOVER";
    
    std::cout << " 发送发现请求..." << std::endl;
    
    if (sendto(discoverySocket, discoveryMsg, strlen(discoveryMsg), 0,
              (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr)) < 0) {
        std::cerr << "发送发现请求失败" << std::endl;
        close(discoverySocket);
        return false;
    }

    // 获取本地网络接口信息，用于多播发送
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            
            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            struct sockaddr_in* netmask = (struct sockaddr_in*)ifa->ifa_netmask;
            
            // 计算网络地址和广播地址
            uint32_t network = addr->sin_addr.s_addr & netmask->sin_addr.s_addr;
            uint32_t broadcast = network | (~netmask->sin_addr.s_addr);
            
            struct sockaddr_in ifaceBroadcastAddr;
            ifaceBroadcastAddr.sin_family = AF_INET;
            ifaceBroadcastAddr.sin_port = htons(DISCOVERY_PORT);
            ifaceBroadcastAddr.sin_addr.s_addr = broadcast;
            
            // 发送到接口的广播地址
            sendto(discoverySocket, discoveryMsg, strlen(discoveryMsg), 0,
                   (struct sockaddr*)&ifaceBroadcastAddr, sizeof(ifaceBroadcastAddr));
        }
        freeifaddrs(ifaddr);
    }

    // 等待服务器响应
    char buffer[256];
    struct sockaddr_in serverAddr;
    socklen_t addrLen = sizeof(serverAddr);
    
    int bytesReceived = recvfrom(discoverySocket, buffer, sizeof(buffer)-1, 0,
                               (struct sockaddr*)&serverAddr, &addrLen);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        
        if (strncmp(buffer, "FILE_SERVER_RESPONSE", 20) == 0) {
            // 解析服务器响应格式: FILE_SERVER_RESPONSE:端口号
            char* portStr = strchr(buffer, ':');
            if (portStr != nullptr) {
                discoveredPort = std::atoi(portStr + 1);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &serverAddr.sin_addr, ipStr, sizeof(ipStr));
                discoveredIP = ipStr;
                std::cout << " 发现服务器: " << discoveredIP << ":" << discoveredPort << std::endl;
                close(discoverySocket);
                return true;
            }
        }
    }

    close(discoverySocket);
    std::cout << " 未发现任何服务器" << std::endl;
    return false;
}

FileAttributes NetworkUtils::getFileAttributes(const std::string& filePath) {
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        throw std::runtime_error("Failed to get file attributes for: " + filePath);
    }

    FileAttributes attrs;
    attrs.permissions = fileStat.st_mode;
    
    #ifdef __APPLE__
        attrs.access_time = fileStat.st_atimespec;
        attrs.modify_time = fileStat.st_mtimespec;
    #else
        attrs.access_time = fileStat.st_atim;
        attrs.modify_time = fileStat.st_mtim;
    #endif
    
    attrs.uid = fileStat.st_uid;
    attrs.gid = fileStat.st_gid;

    return attrs;
}

void NetworkUtils::displayFileAttributes(const std::string& filePath, const FileAttributes& attrs, long fileSize) {
    std::cout << "\n=== 文件属性信息 ===" << std::endl;
    std::cout << "文件: " << filePath << std::endl;
    std::cout << "大小: " << fileSize << " 字节" << std::endl;
    std::cout << "权限: 0" << std::oct << (attrs.permissions & 0777) << std::dec;
    
    std::cout << " (";
    std::cout << ((attrs.permissions & S_IRUSR) ? "r" : "-");
    std::cout << ((attrs.permissions & S_IWUSR) ? "w" : "-");
    std::cout << ((attrs.permissions & S_IXUSR) ? "x" : "-");
    std::cout << ((attrs.permissions & S_IRGRP) ? "r" : "-");
    std::cout << ((attrs.permissions & S_IWGRP) ? "w" : "-");
    std::cout << ((attrs.permissions & S_IXGRP) ? "x" : "-");
    std::cout << ((attrs.permissions & S_IROTH) ? "r" : "-");
    std::cout << ((attrs.permissions & S_IWOTH) ? "w" : "-");
    std::cout << ((attrs.permissions & S_IXOTH) ? "x" : "-") << ")" << std::endl;
    
    std::cout << "用户: " << attrs.uid << std::endl;
    std::cout << "组: " << attrs.gid << std::endl;
    
    char timeBuf[64];
    struct tm tm_time;
    
    localtime_r(&attrs.access_time.tv_sec, &tm_time);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_time);
    std::cout << "访问: " << timeBuf << "." << std::setw(9) << std::setfill('0') 
              << attrs.access_time.tv_nsec << std::setfill(' ') << std::endl;
    
    localtime_r(&attrs.modify_time.tv_sec, &tm_time);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_time);
    std::cout << "修改: " << timeBuf << "." << std::setw(9) << std::setfill('0') 
              << attrs.modify_time.tv_nsec << std::setfill(' ') << std::endl;
    std::cout << "========================\n" << std::endl;
}
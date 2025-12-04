#include "interactive_tcp_client.h"
#include "transfer_handlers.h"
#include <iostream>
#include <string>
#include <sys/stat.h>

InteractiveTCPClient::InteractiveTCPClient(const std::string& ip, int port) 
    : serverIP(ip), serverPort(port) {}

void InteractiveTCPClient::runInteractive() {
    std::cout << "========================================" << std::endl;
    std::cout << "        文件传输客户端 v3.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "   服务器: " << serverIP << ":" << serverPort << std::endl;
    std::cout << "========================================" << std::endl;
        
    while (true) {
        std::cout << "\n请选择操作:" << std::endl;
        std::cout << "1. 顺序传输文件" << std::endl;
        std::cout << "2. 多线程传输文件" << std::endl;
        std::cout << "3. 传输文件夹" << std::endl;
        std::cout << "q. 退出客户端" << std::endl;
        std::cout << "请输入选择 (1/2/3/q): ";
        
        std::string choice;
        std::getline(std::cin, choice);
        
        if (choice == "q" || choice == "Q") {
            std::cout << " 感谢使用，再见！" << std::endl;
            break;
        }
        
        handleUserChoice(choice);
    }
}

void InteractiveTCPClient::handleUserChoice(const std::string& choice) {
    if (choice != "1" && choice != "2" && choice != "3") {
        std::cout << " 无效选择，请输入 1, 2, 3 或 q" << std::endl;
        return;
    }
    
    std::string path;
    if (choice == "1" || choice == "2") {
        std::cout << " 请输入文件路径: ";
        std::getline(std::cin, path);
        
        if (!validateFilePath(path, true)) {
            return;
        }
    } else if (choice == "3") {
        std::cout << " 请输入文件夹路径: ";
        std::getline(std::cin, path);
        
        if (!validateFilePath(path, false)) {
            return;
        }
    }
    
    int threadCount = (choice == "2") ? getThreadCount() : 4;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "       开始传输..." << std::endl;
    std::cout << "========================================\n" << std::endl;

    try {
        TransferHandlers transferHandler(serverIP, serverPort);
        
        if (choice == "1") {
            transferHandler.sequentialTransfer(path);
        } else if (choice == "2") {
            transferHandler.multithreadedTransfer(path, threadCount);
        } else if (choice == "3") {
            transferHandler.directoryTransfer(path);
        }
        
        std::cout << "\n 传输任务完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n 传输失败: " << e.what() << std::endl;
    }
    
    std::cout << "\n" << std::string(40, '=') << std::endl;
}

bool InteractiveTCPClient::validateFilePath(const std::string& path, bool isFile) {
    struct stat pathStat;
    if (stat(path.c_str(), &pathStat) != 0) {
        std::cout << " 路径不存在: " << path << std::endl;
        return false;
    }
    
    if (isFile && !S_ISREG(pathStat.st_mode)) {
        std::cout << " 路径不是普通文件: " << path << std::endl;
        return false;
    }
    
    if (!isFile && !S_ISDIR(pathStat.st_mode)) {
        std::cout << " 路径不是文件夹: " << path << std::endl;
        return false;
    }
    
    return true;
}

int InteractiveTCPClient::getThreadCount() {
    std::cout << " 请输入线程数量 (1-16): ";
    std::string threadInput;
    std::getline(std::cin, threadInput);
    
    try {
        int threadCount = std::stoi(threadInput);
        return std::max(1, std::min(threadCount, 16));
    } catch (...) {
        std::cout << " 使用默认线程数: 4" << std::endl;
        return 4;
    }
}
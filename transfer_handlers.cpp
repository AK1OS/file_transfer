#include "transfer_handlers.h"
#include "network_utils.h"
#include "../common/file_attributes.h"
#include "../common/constants.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

TransferHandlers::TransferHandlers(const std::string& ip, int port) 
    : serverIP(ip), serverPort(port) {}

void TransferHandlers::sequentialTransfer(const std::string& filePath) {
    std::cout << " 启动顺序传输模式..." << std::endl;
    
    auto startTime = std::chrono::steady_clock::now();
    TransferStats stats;
    stats.startTime = startTime;

    try {
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) != 0) {
            throw std::runtime_error("文件不存在: " + filePath);
        }
        
        if (!S_ISREG(fileStat.st_mode)) {
            throw std::runtime_error("路径不是普通文件: " + filePath);
        }
        
        long fileSize = fileStat.st_size;
        stats.fileSize = fileSize;

        std::string fileName = filePath;
        size_t lastSlash = fileName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            fileName = fileName.substr(lastSlash + 1);
        }

        FileAttributes attrs = NetworkUtils::getFileAttributes(filePath);
        NetworkUtils::displayFileAttributes(filePath, attrs, fileSize);

        std::cout << " 连接服务器 " << serverIP << ":" << serverPort << "..." << std::endl;
        int controlSocket = NetworkUtils::createConnection(serverIP, serverPort);

        // 查询断点信息
        ResumeInfo resumeInfo = checkResumeInfo(controlSocket, fileName, fileSize);
        
        long startPos = 0;
        if (resumeInfo.exists && resumeInfo.transferred > 0 && resumeInfo.transferred < fileSize) {
            std::cout << " 发现断点，已传输: " << resumeInfo.transferred << "/" << fileSize << " 字节" << std::endl;
            std::cout << "↩↩ 是否继续传输? (y/n): ";
            std::string choice;
            std::getline(std::cin, choice);
            
            if (choice == "y" || choice == "Y") {
                startPos = resumeInfo.transferred;
                std::cout << " 从 " << startPos << " 字节处继续传输..." << std::endl;
            } else {
                std::cout << " 重新开始传输..." << std::endl;
                // 发送重置命令
                char resetCmd = 'R';
                send(controlSocket, &resetCmd, 1, 0);
                startPos = 0;
            }
        } else {
            std::cout << " 开始新传输..." << std::endl;
            startPos = 0;
        }

        // 关闭当前连接，重新建立传输连接
        close(controlSocket);
        controlSocket = NetworkUtils::createConnection(serverIP, serverPort);

        char mode = 'S';
        if (send(controlSocket, &mode, 1, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送模式标识失败");
        }

        // 发送起始位置
        if (send(controlSocket, &startPos, sizeof(long long), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送起始位置失败");
        }

        if (send(controlSocket, &attrs, sizeof(FileAttributes), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件属性失败");
        }

        int fileNameSize = fileName.size();
        if (send(controlSocket, &fileNameSize, sizeof(int), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件名长度失败");
        }
        
        if (send(controlSocket, fileName.c_str(), fileNameSize, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件名失败");
        }

        if (send(controlSocket, &fileSize, sizeof(long long), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件大小失败");
        }

        std::cout << " 开始传输文件数据..." << std::endl;

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            close(controlSocket);
            throw std::runtime_error("无法打开文件");
        }

        // 跳转到断点位置
        file.seekg(startPos);

        char buffer[BUFFER_SIZE];
        long sent = startPos;
        
        while (!file.eof() && sent < fileSize) {
            file.read(buffer, sizeof(buffer));
            int bytesRead = file.gcount();
            
            if (bytesRead > 0) {
                int bytesSent = send(controlSocket, buffer, bytesRead, 0);
                if (bytesSent <= 0) {
                    file.close();
                    close(controlSocket);
                    throw std::runtime_error("数据传输失败");
                }
                sent += bytesSent;
                stats.totalSent = sent;
                
                double progress = (double)sent / fileSize * 100;
                auto currentTime = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                double speed = (duration > 0) ? (double)(sent - startPos) / duration / 1024 : 0;
                
                std::cout << " 进度: " << std::fixed << std::setprecision(1) << progress 
                          << "%, 速度: " << std::setprecision(2) << speed << " KB/s\r" << std::flush;
            }
        }

        file.close();

        char response[256];
        int bytesReceived = recv(controlSocket, response, sizeof(response) - 1, 0);
        if (bytesReceived > 0) {
            response[bytesReceived] = '\0';
            std::cout << "\n " << response << std::endl;
        }

        close(controlSocket);

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        double avgSpeed = (duration > 0) ? (double)(fileSize - startPos) / duration / 1024 * 1000 : 0;
        
        std::cout << "  传输耗时: " << duration << " ms" << std::endl;
        std::cout << " 平均速度: " << std::fixed << std::setprecision(2) << avgSpeed << " KB/s" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n 顺序传输错误: " << e.what() << std::endl;
        throw;
    }
}

ResumeInfo TransferHandlers::checkResumeInfo(int socket, const std::string& fileName, long fileSize) {
    ResumeInfo info;
    info.fileName = fileName;
    info.fileSize = fileSize;
    info.transferred = 0;
    info.exists = false;

    try {
        // 发送查询命令
        char queryCmd = 'Q';
        if (send(socket, &queryCmd, 1, 0) <= 0) {
            return info;
        }

        // 发送文件名
        int fileNameSize = fileName.size();
        if (send(socket, &fileNameSize, sizeof(int), 0) <= 0) {
            return info;
        }
        if (send(socket, fileName.c_str(), fileNameSize, 0) <= 0) {
            return info;
        }

        // 接收服务器响应
        if (recv(socket, &info.exists, sizeof(bool), MSG_WAITALL) != sizeof(bool)) {
            return info;
        }

        if (info.exists) {
            if (recv(socket, &info.transferred, sizeof(long long), MSG_WAITALL) != sizeof(long long)) {
                info.exists = false;
            }
        }

    } catch (...) {
        info.exists = false;
    }

    return info;
}

void TransferHandlers::multithreadedTransfer(const std::string& filePath, int numThreads) {
    std::cout << " 启动多线程传输模式 (" << numThreads << " 线程)..." << std::endl;
    
    auto startTime = std::chrono::steady_clock::now();
    TransferStats stats;
    stats.startTime = startTime;

    try {
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) != 0) {
            throw std::runtime_error("文件不存在: " + filePath);
        }
        
        if (!S_ISREG(fileStat.st_mode)) {
            throw std::runtime_error("路径不是普通文件: " + filePath);
        }
        
        long fileSize = fileStat.st_size;
        if (fileSize == 0) {
            throw std::runtime_error("文件为空: " + filePath);
        }
        stats.fileSize = fileSize;

        std::string fileName = filePath;
        size_t lastSlash = fileName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            fileName = fileName.substr(lastSlash + 1);
        }

        FileAttributes attrs = NetworkUtils::getFileAttributes(filePath);
        NetworkUtils::displayFileAttributes(filePath, attrs, fileSize);

        std::cout << " 发送控制信息到服务器..." << std::endl;
        
        int controlSocket = NetworkUtils::createConnection(serverIP, serverPort);
        char mode = 'M';
        if (send(controlSocket, &mode, 1, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送模式标识失败");
        }

        if (send(controlSocket, &numThreads, sizeof(int), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送线程数失败");
        }

        if (send(controlSocket, &attrs, sizeof(FileAttributes), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件属性失败");
        }

        int fileNameSize = fileName.size();
        if (send(controlSocket, &fileNameSize, sizeof(int), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件名长度失败");
        }
        
        if (send(controlSocket, fileName.c_str(), fileNameSize, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件名失败");
        }
        
        if (send(controlSocket, &fileSize, sizeof(long), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件大小失败");
        }

        int sessionId;
        if (recv(controlSocket, &sessionId, sizeof(int), MSG_WAITALL) != sizeof(int)) {
            close(controlSocket);
            throw std::runtime_error("接收会话ID失败");
        }

        close(controlSocket);

        long chunkSize = fileSize / numThreads;
        long lastChunkSize = fileSize - (chunkSize * (numThreads - 1));

        std::cout << " 文件分块: " << numThreads << " 个块, 块大小: ~" << chunkSize/1024 << " KB" << std::endl;
        std::cout << " 会话ID: " << sessionId << std::endl;
        std::cout << " 启动多线程传输..." << std::endl;

        std::vector<std::thread> threads;
        std::vector<bool> threadSuccess(numThreads, true);
        std::mutex errorMutex;
        std::string errorMessage;

        for (int i = 0; i < numThreads; i++) {
            long startPos = i * chunkSize;
            long currentChunkSize = (i == numThreads - 1) ? lastChunkSize : chunkSize;
            
            threads.emplace_back([this, i, sessionId, startPos, currentChunkSize, filePath, &stats, &threadSuccess, &errorMutex, &errorMessage]() {
                try {
                    sendChunk(i, sessionId, startPos, currentChunkSize, filePath, stats);
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    threadSuccess[i] = false;
                    errorMessage = e.what();
                }
            });
        }

        bool allThreadsSuccess = true;
        while (stats.completedChunks < numThreads) {
            long currentSent = stats.totalSent;
            double progress = (double)currentSent / fileSize * 100;
            
            auto currentTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
            double speed = (duration > 0) ? (double)currentSent / duration / 1024 : 0;
            
            std::cout << " 进度: " << std::fixed << std::setprecision(1) << progress 
                      << "%, 速度: " << std::setprecision(2) << speed << " KB/s, "
                      << "完成块: " << stats.completedChunks << "/" << numThreads << "\r" << std::flush;
            
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (!errorMessage.empty()) {
                    allThreadsSuccess = false;
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        if (!allThreadsSuccess) {
            throw std::runtime_error("多线程传输失败: " + errorMessage);
        }

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        double avgSpeed = (duration > 0) ? (double)fileSize / duration / 1024 * 1000 : 0;
        
        std::cout << "\n 多线程传输完成!" << std::endl;
        std::cout << "  传输耗时: " << duration << " ms" << std::endl;
        std::cout << " 平均速度: " << std::fixed << std::setprecision(2) << avgSpeed << " KB/s" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n 多线程传输错误: " << e.what() << std::endl;
        throw;
    }
}

void TransferHandlers::sendChunk(int chunkIndex, int sessionId, long startPos, long chunkSize, 
                              const std::string& filePath, TransferStats& stats) {
    int chunkSocket = -1;
    try {
        chunkSocket = NetworkUtils::createConnection(serverIP, serverPort);

        int header[4] = {sessionId, chunkIndex, static_cast<int>(startPos), static_cast<int>(chunkSize)};
        ssize_t totalSent = 0;
        while (totalSent < static_cast<ssize_t>(sizeof(header))) {
            ssize_t sent = send(chunkSocket, reinterpret_cast<char*>(header) + totalSent, 
                               sizeof(header) - totalSent, 0);
            if (sent <= 0) {
                throw std::runtime_error("发送块头信息失败");
            }
            totalSent += sent;
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("无法打开文件: " + filePath);
        }
        file.seekg(startPos);

        std::vector<char> buffer(BUFFER_SIZE);
        long sent = 0;
        
        while (sent < chunkSize) {
            long remaining = chunkSize - sent;
            long toRead = std::min(remaining, static_cast<long>(buffer.size()));
            
            file.read(buffer.data(), toRead);
            int bytesRead = file.gcount();
            
            if (bytesRead > 0) {
                ssize_t bytesSent = 0;
                while (bytesSent < bytesRead) {
                    ssize_t result = send(chunkSocket, buffer.data() + bytesSent, bytesRead - bytesSent, 0);
                    if (result <= 0) {
                        throw std::runtime_error("发送块数据失败");
                    }
                    bytesSent += result;
                }
                sent += bytesRead;
                stats.totalSent += bytesRead;
            } else {
                break;
            }
        }

        file.close();
        
        char ack;
        if (recv(chunkSocket, &ack, 1, 0) <= 0) {
            throw std::runtime_error("未收到服务器确认");
        }
        
        if (chunkSocket >= 0) {
            close(chunkSocket);
            chunkSocket = -1;
        }

        stats.completedChunks++;
        
        {
            std::lock_guard<std::mutex> lock(stats.consoleMutex);
            std::cout << " 块 " << chunkIndex << " 传输完成 (" << chunkSize << " 字节)" << std::endl;
        }
    } catch (const std::exception& e) {
        if (chunkSocket >= 0) {
            close(chunkSocket);
        }
        std::lock_guard<std::mutex> lock(stats.consoleMutex);
        std::cerr << " 块 " << chunkIndex << " 错误: " << e.what() << std::endl;
        throw;
    }
}

void TransferHandlers::directoryTransfer(const std::string& dirPath) {
    std::cout << " 启动文件夹传输模式..." << std::endl;
    
    auto startTime = std::chrono::steady_clock::now();

    try {
        struct stat dirStat;
        if (stat(dirPath.c_str(), &dirStat) != 0) {
            throw std::runtime_error("目录不存在: " + dirPath);
        }
        
        if (!S_ISDIR(dirStat.st_mode)) {
            throw std::runtime_error("路径不是目录: " + dirPath);
        }

        std::string dirName = dirPath;
        size_t lastSlash = dirName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            dirName = dirName.substr(lastSlash + 1);
        }

        std::cout << " 连接服务器 " << serverIP << ":" << serverPort << "..." << std::endl;
        int controlSocket = NetworkUtils::createConnection(serverIP, serverPort);

        char mode = 'D';
        if (send(controlSocket, &mode, 1, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送模式标识失败");
        }

        int baseNameLen = dirName.size();
        if (send(controlSocket, &baseNameLen, sizeof(int), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送基础目录名长度失败");
        }
        
        if (send(controlSocket, dirName.c_str(), baseNameLen, 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送基础目录名失败");
        }

        std::vector<std::pair<std::string, bool>> fileList;
        scanDirectory(dirPath, "", fileList);
        
        int totalItems = fileList.size();
        if (send(controlSocket, &totalItems, sizeof(int), 0) <= 0) {
            close(controlSocket);
            throw std::runtime_error("发送文件数量失败");
        }

        std::cout << " 发现 " << totalItems << " 个文件/目录" << std::endl;
        std::cout << " 开始传输文件夹内容..." << std::endl;

        int successCount = 0;
        int failCount = 0;

        for (size_t i = 0; i < fileList.size(); i++) {
            const auto& item = fileList[i];
            std::string fullPath = dirPath + "/" + item.first;
            
            bool success = false;
            if (item.second) {
                success = sendDirectoryItem(controlSocket, item.first, fullPath);
            } else {
                success = sendDirectoryFile(controlSocket, item.first, fullPath);
            }

            if (success) {
                successCount++;
            } else {
                failCount++;
            }

            std::cout << " 进度: " << (i+1) << "/" << totalItems 
                      << " (成功: " << successCount << ", 失败: " << failCount << ")\r" << std::flush;
        }

        char response[1024];
        int bytesReceived = recv(controlSocket, response, sizeof(response) - 1, 0);
        if (bytesReceived > 0) {
            response[bytesReceived] = '\0';
            std::cout << "\n " << response << std::endl;
        }

        close(controlSocket);

        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        
        std::cout << "  传输耗时: " << duration << " ms" << std::endl;
        std::cout << " 统计: 成功 " << successCount << " 个, 失败 " << failCount << " 个" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n 文件夹传输错误: " << e.what() << std::endl;
        throw;
    }
}

void TransferHandlers::scanDirectory(const std::string& basePath, const std::string& relativePath, 
                                  std::vector<std::pair<std::string, bool>>& result) {
    std::string fullPath = basePath;
    if (!relativePath.empty()) {
        fullPath += "/" + relativePath;
    }

    DIR* dir = opendir(fullPath.c_str());
    if (!dir) {
        std::cerr << "无法打开目录: " << fullPath << std::endl;
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string itemRelativePath = relativePath.empty() ? 
            entry->d_name : relativePath + "/" + entry->d_name;
        
        std::string itemFullPath = fullPath + "/" + entry->d_name;
        
        struct stat statBuf;
        if (stat(itemFullPath.c_str(), &statBuf) == 0) {
            if (S_ISDIR(statBuf.st_mode)) {
                result.emplace_back(itemRelativePath, true);
                scanDirectory(basePath, itemRelativePath, result);
            } else {
                result.emplace_back(itemRelativePath, false);
            }
        }
    }
    closedir(dir);
}

bool TransferHandlers::sendDirectoryItem(int socket, const std::string& relativePath, const std::string& fullPath) {
    try {
        char itemType = 'D';
        if (send(socket, &itemType, 1, 0) <= 0) {
            return false;
        }

        int pathLen = relativePath.size();
        if (send(socket, &pathLen, sizeof(int), 0) <= 0) {
            return false;
        }

        if (send(socket, relativePath.c_str(), pathLen, 0) <= 0) {
            return false;
        }

        FileAttributes attrs = NetworkUtils::getFileAttributes(fullPath);
        if (send(socket, &attrs, sizeof(FileAttributes), 0) <= 0) {
            return false;
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool TransferHandlers::sendDirectoryFile(int socket, const std::string& relativePath, const std::string& fullPath) {
    try {
        char itemType = 'F';
        if (send(socket, &itemType, 1, 0) <= 0) {
            return false;
        }

        int pathLen = relativePath.size();
        if (send(socket, &pathLen, sizeof(int), 0) <= 0) {
            return false;
        }

        if (send(socket, relativePath.c_str(), pathLen, 0) <= 0) {
            return false;
        }

        FileAttributes attrs = NetworkUtils::getFileAttributes(fullPath);
        if (send(socket, &attrs, sizeof(FileAttributes), 0) <= 0) {
            return false;
        }

        struct stat fileStat;
        if (stat(fullPath.c_str(), &fileStat) != 0) {
            return false;
        }

        long long fileSize = fileStat.st_size;
        if (send(socket, &fileSize, sizeof(long long), 0) <= 0) {
            return false;
        }

        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        std::vector<char> buffer(BUFFER_SIZE);
        long long sent = 0;
        
        while (sent < fileSize) {
            file.read(buffer.data(), buffer.size());
            int bytesRead = file.gcount();
            
            if (bytesRead > 0) {
                int bytesSent = 0;
                while (bytesSent < bytesRead) {
                    int result = send(socket, buffer.data() + bytesSent, bytesRead - bytesSent, 0);
                    if (result <= 0) {
                        file.close();
                        return false;
                    }
                    bytesSent += result;
                }
                sent += bytesRead;
            } else {
                break;
            }
        }

        file.close();
        return sent == fileSize;
    } catch (...) {
        return false;
    }
}
#ifndef TRANSFER_HANDLERS_H
#define TRANSFER_HANDLERS_H

#include <string>
#include "../common/transfer_stats.h"
#include <vector>

struct ResumeInfo {
    long long fileSize;
    long long transferred;
    bool exists;
    std::string fileName;
};

class TransferHandlers {
private:
    std::string serverIP;
    int serverPort;

public:
    TransferHandlers(const std::string& ip, int port);
    void sequentialTransfer(const std::string& filePath);
    void multithreadedTransfer(const std::string& filePath, int numThreads = 4);
    void directoryTransfer(const std::string& dirPath);

private:
    ResumeInfo checkResumeInfo(int socket, const std::string& fileName, long fileSize);
    void sendChunk(int chunkIndex, int sessionId, long startPos, long chunkSize, 
                  const std::string& filePath, TransferStats& stats);
    void scanDirectory(const std::string& basePath, const std::string& relativePath, 
                      std::vector<std::pair<std::string, bool>>& result);
    bool sendDirectoryItem(int socket, const std::string& relativePath, const std::string& fullPath);
    bool sendDirectoryFile(int socket, const std::string& relativePath, const std::string& fullPath);
};

#endif
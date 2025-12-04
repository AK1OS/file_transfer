#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <string>
#include "../common/file_attributes.h"

class NetworkUtils {
public:
    static int createConnection(const std::string& serverIP, int serverPort);
    static bool discoverServer(std::string& discoveredIP, int& discoveredPort);
    static FileAttributes getFileAttributes(const std::string& filePath);
    static void displayFileAttributes(const std::string& filePath, 
                                   const FileAttributes& attrs, long fileSize);
};

#endif
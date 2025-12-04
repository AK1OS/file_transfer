#ifndef INTERACTIVE_TCP_CLIENT_H
#define INTERACTIVE_TCP_CLIENT_H

#include <string>

class InteractiveTCPClient {
private:
    std::string serverIP;
    int serverPort;

public:
    InteractiveTCPClient(const std::string& ip, int port);
    bool discoverServer(std::string& discoveredIP, int& discoveredPort);
    void runInteractive();

private:
    void handleUserChoice(const std::string& choice);
    bool validateFilePath(const std::string& path, bool isFile);
    int getThreadCount();
};

#endif
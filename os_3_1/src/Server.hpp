#pragma once

#define WINDOW_WIDTH 720
#define WINDOW_HEIGHT 720
#define SOCKET_PATH "/tmp/chat_server.sock"

#include "UDS.hpp"
#include "Interface.hpp"

#include <SFML/System.hpp>
#include <SFML/Window.hpp>
#include <SFML/Graphics.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Server
{
private:
    static constexpr unsigned short SERVER_LOG_BOX = 0;
    static constexpr unsigned short CHAT_BOX       = 1;

    sf::RenderWindow* window;
    sf::Clock* clock;
    Interface* ui;

    std::vector<std::shared_ptr<UDS>> clients;
    std::mutex clientsMutex;
    std::atomic<bool> running{ true };

    struct Incoming { 
        std::shared_ptr<UDS> from; 
        std::string data; 
    };
    std::queue<Incoming> incoming;
    std::mutex incomingMutex;

    std::thread acceptThread;
    UDSListener* listener = nullptr;

    std::queue<std::string> pendingLogs;
    std::mutex pendingLogsMutex;

    void Log(const std::string& msg);
    void AcceptLoop();
    void ReadClientThread(std::shared_ptr<UDS> uds);
    void Broadcast(const std::string& data);

public:
    Server();
    ~Server();
    void Run();
    void RunNetwork();
    void RunPackets();
    void RunInterface();
};
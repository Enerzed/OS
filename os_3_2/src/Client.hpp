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
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class Client
{
private:
    static constexpr unsigned short LOG_BOX  = 0;
    static constexpr unsigned short CHAT_BOX = 1;

    sf::RenderWindow* window;
    sf::Clock* clock;
    Interface* m_ui;

    UDS uds;
    std::thread netThread;

    std::atomic<bool> running{ true };
    std::atomic<bool> connected{ false };

    std::queue<std::string> incoming;
    std::mutex incomingMutex;

    std::queue<std::string> outgoing;
    std::mutex outgoingMutex;
    std::condition_variable outgoingCV;

    std::queue<std::string> pendingLogs;
    std::mutex pendingLogsMutex;

    void Log(const std::string& msg);
    void NetLoop();
    void WriteLoop();
    void DrainIncoming();
    void RunInterface();
    void SendMessage(const std::string& msg);

public:
    Client();
    ~Client();
    void Run();
};
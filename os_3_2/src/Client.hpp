#pragma once

#define WINDOW_WIDTH 720
#define WINDOW_HEIGHT 720
#define PIPE_NAME L"\\\\.\\pipe\\ChatServer"

#include "Pipes.hpp"
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
    sf::Time* time;
    Interface* m_ui;

    Pipes pipe;
    std::thread netThread;

    std::atomic<bool> running{ true };
    std::atomic<bool> connected{ false };

    // read: netThread -> UI
    std::queue<std::string> incoming;
    std::mutex incomingMutex;

    // write: UI -> netThread (единственный поток, который трогает pipe.Write)
    std::queue<std::string> outgoing;
    std::mutex outgoingMutex;
    std::condition_variable outgoingCV;

    // logs из net-потока -> UI
    std::queue<std::string> pendingLogs;
    std::mutex pendingLogsMutex;

    void Log(const std::string& msg);
    void NetLoop();     // сеть: connect + приём
    void WriteLoop();   // единственный писатель в pipe
    void DrainIncoming();
    void RunInterface();
    void SendMessage(const std::string& msg);

public:
    Client();
    ~Client();
    void Run();
};
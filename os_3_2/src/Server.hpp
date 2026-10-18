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
    sf::Time* time;
    Interface* ui;

    std::vector<Pipes*> clients;
    std::mutex clientsMutex;
    std::atomic<bool> running{ true };

    struct Incoming { Pipes* from; std::string data; };
    std::queue<Incoming> incoming;
    std::mutex incomingMutex;

    std::thread acceptThread;

    std::queue<std::string> pendingLogs;
    std::mutex pendingLogsMutex;

    void Log(const std::string& msg);
    void AcceptLoop();
    void ReadClientThread(Pipes*);
    void Broadcast(const std::string& data, Pipes* exclude = nullptr);

public:
    Server();
    ~Server();
    void Run();
    void RunNetwork();
    void RunSystemMessages();
    void RunPackets();
    void RunInterface();
};
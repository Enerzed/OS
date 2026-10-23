#include "Server.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>

Server::Server()
{
    window = new sf::RenderWindow(sf::VideoMode({ WINDOW_WIDTH, WINDOW_HEIGHT }), "Chat Server", sf::Style::Close);
    window->setFramerateLimit(120);

    clock = new sf::Clock;

    ui = new Interface;
    ui->Init(*window);
    ui->AddTextBox(SERVER_LOG_BOX, "Server log");
    ui->AddTextBox(CHAT_BOX,       "Chat");
}

Server::~Server()
{
    running = false;

    if (listener)
    {
        listener->Close(); 
    }
    if (acceptThread.joinable())
    {
        acceptThread.join();
    }
    if (listener)
    {
        delete listener;
        listener = nullptr;
    }

    {
        std::lock_guard lock(clientsMutex);
        for (auto& p : clients) p->Close();
        clients.clear();
    }

    ui->Shutdown();
    delete ui;
    delete window;
    delete clock;
}

void Server::Log(const std::string& msg)
{
    std::lock_guard lock(pendingLogsMutex);
    pendingLogs.push(msg);
}

void Server::RunNetwork()
{
    listener = new UDSListener(SOCKET_PATH);
    Log("[net] listening on " + std::string(SOCKET_PATH));
    acceptThread = std::thread(&Server::AcceptLoop, this);
}

void Server::AcceptLoop()
{
    while (running)
    {
        int client_fd = listener->Accept();
        if (client_fd < 0) break; 

        auto uds = std::make_shared<UDS>(client_fd);

        {
            std::lock_guard lock(clientsMutex);
            clients.push_back(uds);
        }

        Log("[net] client connected");
        std::thread(&Server::ReadClientThread, this, uds).detach();
    }
    Log("[net] accept loop stopped");
}

void Server::ReadClientThread(std::shared_ptr<UDS> uds)
{
    std::string data;
    while (running && uds->Read(data))
    {
        std::lock_guard lock(incomingMutex);
        incoming.push({uds, std::move(data) });
        data.clear();
    }

    {
        std::lock_guard lock(clientsMutex);
        clients.erase(std::remove(clients.begin(), clients.end(), uds), clients.end());
    }

    Log("[net] client disconnected");
}

void Server::Broadcast(const std::string& data)
{
    std::lock_guard lock(clientsMutex);
    for (auto& p : clients) {
        p->Write(data);
    }
}

void Server::RunPackets()
{
    std::queue<Incoming> local;
    {
        std::lock_guard lock(incomingMutex);
        std::swap(local, incoming);
    }

    while (!local.empty())
    {
        Incoming msg = std::move(local.front());
        local.pop();

        ui->TextBoxMessage("", msg.data, CHAT_BOX);
        Broadcast(msg.data);
    }
}

void Server::RunInterface()
{
    {
        std::lock_guard lock(pendingLogsMutex);
        while (!pendingLogs.empty())
        {
            ui->TextBoxSystemMessage(pendingLogs.front(), SERVER_LOG_BOX);
            pendingLogs.pop();
        }
    }
    ui->Render();
}

void Server::Run()
{
    RunNetwork();

    while (window->isOpen())
    {
        while (const std::optional event = window->pollEvent())
        {
            ImGui::SFML::ProcessEvent(*window, *event);
            if (event->is<sf::Event::Closed>())
                window->close();
        }

        ui->Update(*window, clock->restart());

        RunPackets();
        RunInterface();

        window->clear();
        ImGui::SFML::Render(*window);
        window->display();
    }

    running = false;
}
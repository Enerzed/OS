#include "Server.hpp"

#include <chrono>
#include <iostream>

Server::Server()
{
    window = new sf::RenderWindow(sf::VideoMode({ WINDOW_WIDTH, WINDOW_HEIGHT }), "Chat Server", sf::Style::Close);
    window->setFramerateLimit(120);

    clock = new sf::Clock;
    time  = new sf::Time;

    ui = new Interface;
    ui->Init(*window);
    ui->AddTextBox(SERVER_LOG_BOX, "Server log");
    ui->AddTextBox(CHAT_BOX,       "Chat");
}

Server::~Server()
{
    running = false;

    if (acceptThread.joinable())
    {
        CancelSynchronousIo(acceptThread.native_handle());
        acceptThread.join();
    }

    {
        std::lock_guard lock(clientsMutex);
        for (Pipes* p : clients) p->Close();
        clients.clear();
    }

    ui->Shutdown();

    delete ui;
    delete window;
    delete clock;
    delete time;
}

// ---------------------------------------------------------------------
//  Логирование
// ---------------------------------------------------------------------
void Server::Log(const std::string& msg)
{
    std::lock_guard lock(pendingLogsMutex);
    pendingLogs.push(msg);
    std::cout << msg << std::endl;
}

// ---------------------------------------------------------------------
//  Сеть
// ---------------------------------------------------------------------
void Server::RunNetwork()
{
    Log("[net] pipe name: \\\\.\\pipe\\ChatServer");
    acceptThread = std::thread(&Server::AcceptLoop, this);
}

void Server::AcceptLoop()
{
    while (running)
    {
        Pipes* pipe = new Pipes(PIPE_NAME);

        if (!pipe->IsValid())
        {
            Log("[net] CreateNamedPipe failed: " + std::to_string(GetLastError()));
            delete pipe;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!pipe->WaitForClient())
        {
            delete pipe;
            continue;
        }

        if (!running) { delete pipe; break; }

        size_t count;
        {
            std::lock_guard lock(clientsMutex);
            clients.push_back(pipe);
            count = clients.size();
        }
        Log("[net] client connected (total: " + std::to_string(count) + ")");

        std::thread(&Server::ReadClientThread, this, pipe).detach();
    }

    Log("[net] accept loop stopped");
}

void Server::ReadClientThread(Pipes* pipe)
{
    std::string data;
    while (running && pipe->Read(data))
    {
        Log("[debug] Server received: " + data);
        std::lock_guard lock(incomingMutex);
        incoming.push({ pipe, std::move(data) });
        data.clear();
    }

    {
        std::lock_guard lock(clientsMutex);
        for (auto it = clients.begin(); it != clients.end(); ++it)
        {
            if (*it == pipe) { clients.erase(it); break; }
        }
    }

    pipe->Close();
    delete pipe;

    Log("[net] client disconnected");
}

void Server::Broadcast(const std::string& data, Pipes* exclude)
{
    std::lock_guard lock(clientsMutex);
    for (Pipes* p : clients)
        if (p != exclude) p->Write(data);
}

// ---------------------------------------------------------------------
//  Пакеты (главный поток)
// ---------------------------------------------------------------------
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

        // MVP-протокол: сырая строка.
        // В CHAT_BOX — сам текст (без префикса [system]), в консоль — с пометкой.
        ui->TextBoxMessage("", msg.data, CHAT_BOX);
        std::cout << "[chat] " << msg.data << std::endl;

        Broadcast(msg.data);
    }
}

void Server::RunSystemMessages()
{
    // сюда можно добавить join/leave уведомления
}

// ---------------------------------------------------------------------
//  UI (главный поток)
// ---------------------------------------------------------------------
void Server::RunInterface()
{
    // Перенос отложенных логов в UI-бокс
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

// ---------------------------------------------------------------------
//  Главный цикл
// ---------------------------------------------------------------------
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
        RunSystemMessages();
        RunInterface();

        window->clear();
        ImGui::SFML::Render(*window);
        window->display();
    }

    running = false;
}
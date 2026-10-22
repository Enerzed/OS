/*
 * Client.cpp
 */

#include "Client.hpp"

#include <chrono>
#include <iostream>

Client::Client()
{
    window = new sf::RenderWindow(
        sf::VideoMode({ WINDOW_WIDTH, WINDOW_HEIGHT }),
        "Chat Client",
        sf::Style::Close);
    window->setFramerateLimit(120);

    clock = new sf::Clock;
    time  = new sf::Time;

    m_ui = new Interface;
    m_ui->Init(*window);
    m_ui->AddTextBox(LOG_BOX,  "Connection");
    m_ui->AddTextBox(CHAT_BOX, "Chat");

    // Строка ввода в чат-боксе: Enter или кнопка Send -> SendMessage
    m_ui->SetTextBoxInput(
        CHAT_BOX,
        [this](const std::string& text) { SendMessage(text); },
        "Type a message and press Enter...");
}

Client::~Client()
{
    running = false;
    outgoingCV.notify_all();             // разбудить WriteLoop

    // Снимаем блокирующие ReadFile/WriteFile в net-потоке
    if (netThread.joinable())
    {
        CancelSynchronousIo(netThread.native_handle());
        netThread.join();
    }

    // Ещё раз — на случай, если WriteLoop висел в WriteFile
    // (после running=false он выйдет по таймауту CV)
    pipe.Close();
    m_ui->Shutdown();

    delete m_ui;
    delete window;
    delete clock;
    delete time;
}

void Client::Log(const std::string& msg)
{
    std::lock_guard lock(pendingLogsMutex);
    pendingLogs.push(msg);
    std::cout << msg << std::endl;
}

// ---------------------------------------------------------------------
//  Сетевой поток: connect + recv, авто-reconnect
// ---------------------------------------------------------------------
void Client::NetLoop()
{
    while (running)
    {
        if (!connected)
        {
            Log("[net] connecting to \\\\.\\pipe\\ChatServer ...");
            if (!pipe.Connect(PIPE_NAME, 3000))
            {
                Log("[net] server not reachable, will retry");
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            connected = true;
            Log("[net] connected");
            outgoingCV.notify_all();     // разбудить WriteLoop

            // Поток-писатель на время жизни этого соединения
            std::thread(&Client::WriteLoop, this).detach();
        }

        std::string data;
        while (running && connected && pipe.Read(data))
        {
            std::lock_guard lock(incomingMutex);
            incoming.push(std::move(data));
            data.clear();
        }

        if (connected)
        {
            connected = false;
            pipe.Close();
            Log("[net] disconnected");
            outgoingCV.notify_all();
        }

        if (running)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Client::WriteLoop()
{
    while (running)
    {
        std::string msg;
        {
            std::unique_lock lock(outgoingMutex);
            outgoingCV.wait(lock, [this] {
                return !outgoing.empty() || !running || !connected;
            });

            if (!running) return;

            if (!connected)
            {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            msg = std::move(outgoing.front());
            outgoing.pop();
        }

        Log("[debug] WriteLoop sending: " + msg); // <-- ДОБАВИТЬ

        if (!pipe.Write(msg))
        {
            Log("[net] write failed");
            connected = false;
        }
        else
        {
            Log("[debug] WriteLoop success!"); // <-- ДОБАВИТЬ
        }
    }
}

// ---------------------------------------------------------------------
//  Отправка из UI-потока
// ---------------------------------------------------------------------
void Client::SendMessage(const std::string& msg)
{
    Log("[debug] SendMessage: " + msg);
    if (!connected)
    {
        Log("[net] cannot send: not connected");
        return;
    }

    {
        std::lock_guard lock(outgoingMutex);
        outgoing.push(msg);
    }
    outgoingCV.notify_one();
}

// ---------------------------------------------------------------------
//  Перенос принятого из очереди в UI (главный поток)
// ---------------------------------------------------------------------
void Client::DrainIncoming()
{
    std::queue<std::string> local;
    {
        std::lock_guard lock(incomingMutex);
        std::swap(local, incoming);
    }

    while (!local.empty())
    {
        m_ui->TextBoxMessage("", local.front(), CHAT_BOX);
        local.pop();
    }
}

void Client::RunInterface()
{
    {
        std::lock_guard lock(pendingLogsMutex);
        while (!pendingLogs.empty())
        {
            m_ui->TextBoxSystemMessage(pendingLogs.front(), LOG_BOX);
            pendingLogs.pop();
        }
    }

    DrainIncoming();
    m_ui->Render();
}

// ---------------------------------------------------------------------
//  Главный цикл
// ---------------------------------------------------------------------
void Client::Run()
{
    netThread = std::thread(&Client::NetLoop, this);

    while (window->isOpen())
    {
        while (const std::optional event = window->pollEvent())
        {
            ImGui::SFML::ProcessEvent(*window, *event);
            if (event->is<sf::Event::Closed>())
                window->close();
        }

        m_ui->Update(*window, clock->restart());
        RunInterface();

        window->clear();
        ImGui::SFML::Render(*window);
        window->display();
    }

    running = false;
}
/*
 * Pipes.hpp
 */

#pragma once

#include <windows.h>
#include <string>

// Один экземпляр именованного канала = одно клиентское подключение.
// Создание канала и ожидание клиента — в конструкторе/методе WaitForClient.
class Pipes
{
public:
    explicit Pipes(const std::wstring& name);
    ~Pipes();

    Pipes(const Pipes&) = delete;
    Pipes& operator=(const Pipes&) = delete;

    bool IsValid() const { return hPipe != INVALID_HANDLE_VALUE; }

    // Блокирующее ожидание подключения клиента к этому экземпляру.
    bool WaitForClient();

    // Блокирующее чтение одного сообщения.
    bool Read(std::string& out);

    // Запись одного сообщения.
    bool Write(const std::string& data);

    void Close();

    HANDLE GetHandle() const { return hPipe; }

private:
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    std::wstring name;
};
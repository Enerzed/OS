/*
 * Pipes.hpp
 */

#pragma once

#include <windows.h>
#include <string>

class Pipes
{
public:
    Pipes() = default;
    ~Pipes();

    Pipes(const Pipes&) = delete;
    Pipes& operator=(const Pipes&) = delete;

    bool IsConnected() const { return hPipe != INVALID_HANDLE_VALUE; }

    // Пытается подключиться в течение totalTimeoutMs.
    // Сам обрабатывает ERROR_FILE_NOT_FOUND (сервер ещё не поднят)
    // и ERROR_PIPE_BUSY (все экземпляры заняты).
    bool Connect(const std::wstring& name, int totalTimeoutMs);

    bool Read(std::string& out);
    bool Write(const std::string& data);
    void Close();

private:
    HANDLE hPipe = INVALID_HANDLE_VALUE;
};
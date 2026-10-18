/*
 * Pipes.cpp
 */

#include "Pipes.hpp"

Pipes::Pipes(const std::wstring& name)
    : name(name)
{
    // PIPE_UNLIMITED_INSTANCES позволяет создавать много экземпляров
    // с одинаковым именем — по одному на каждого клиента.
    hPipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096,   // размер выходного буфера
        4096,   // размер входного буфера
        0,      // таймаут по умолчанию
        nullptr // атрибуты безопасности по умолчанию
    );
}

Pipes::~Pipes()
{
    Close();
}

bool Pipes::WaitForClient()
{
    if (!IsValid()) return false;

    BOOL ok = ConnectNamedPipe(hPipe, nullptr);
    if (ok) return true;

    // ERROR_PIPE_CONNECTED = клиент подключился между CreateNamedPipe и ConnectNamedPipe
    return GetLastError() == ERROR_PIPE_CONNECTED;
}

bool Pipes::Read(std::string& out)
{
    if (!IsValid()) return false;

    char buffer[4096];
    DWORD bytesRead = 0;

    BOOL ok = ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, nullptr);
    if (!ok || bytesRead == 0) return false;

    out.assign(buffer, bytesRead);
    return true;
}

bool Pipes::Write(const std::string& data)
{
    if (!IsValid()) return false;

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hPipe, data.data(), (DWORD)data.size(), &bytesWritten, nullptr);
    return ok && bytesWritten == data.size();
}

void Pipes::Close()
{
    if (hPipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}
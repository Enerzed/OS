/*
 * Pipes.cpp
 */

#include "Pipes.hpp"

Pipes::~Pipes()
{
    Close();
}

bool Pipes::Connect(const std::wstring& name, int totalTimeoutMs)
{
    Close();

    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < (ULONGLONG)totalTimeoutMs)
    {
        hPipe = CreateFileW(
            name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);

        if (hPipe != INVALID_HANDLE_VALUE)
        {
            // Явно переводим клиентскую сторону в message-mode, чтобы
            // каждый ReadFile возвращал ровно одно WriteFile сервера.
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr))
            {
                CloseHandle(hPipe);
                hPipe = INVALID_HANDLE_VALUE;
                return false;
            }
            return true;
        }

        switch (GetLastError())
        {
        case ERROR_PIPE_BUSY:
            // Сервер есть, но все экземпляры заняты — ждём свободный.
            WaitNamedPipeW(name.c_str(), 500);
            break;
        default:
            // ERROR_FILE_NOT_FOUND и прочее — сервер не запущен,
            // просто ждём и пробуем снова.
            Sleep(500);
            break;
        }
    }

    return false;
}

bool Pipes::Read(std::string& out)
{
    if (!IsConnected()) return false;

    char buffer[4096];
    DWORD bytesRead = 0;

    BOOL ok = ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, nullptr);
    if (!ok || bytesRead == 0) return false;

    out.assign(buffer, bytesRead);
    return true;
}

bool Pipes::Write(const std::string& data)
{
    if (!IsConnected()) return false;

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hPipe, data.data(), (DWORD)data.size(), &bytesWritten, nullptr);
    return ok && bytesWritten == data.size();
}

void Pipes::Close()
{
    if (hPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
}
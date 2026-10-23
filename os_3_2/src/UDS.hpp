#pragma once

#include <string>
#include <cstdint>

class UDS
{
public:
    UDS() = default;
    ~UDS();

    bool Connect(const std::string& path, int timeout_ms);
    bool Read(std::string& out);
    bool Write(const std::string& data);
    void Close();
    bool IsConnected() const { return fd >= 0; }

private:
    int fd = -1;
};
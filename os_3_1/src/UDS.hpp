#pragma once

#include <string>
#include <cstdint>

class UDSListener
{
public:
    explicit UDSListener(const std::string& path);
    ~UDSListener();

    int Accept();
    void Close();

private:
    int server_fd = -1;
    std::string path;
};

class UDS
{
public:
    explicit UDS(int fd);
    ~UDS();

    bool Read(std::string& out);
    bool Write(const std::string& data);
    void Close();

private:
    int fd = -1;
};
#include "UDS.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

static bool read_exact(int fd, void* buf, size_t len)
{
    size_t total = 0;
    char* p = static_cast<char*>(buf);
    while (total < len)
    {
        ssize_t n = read(fd, p + total, len - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

static bool write_exact(int fd, const void* buf, size_t len)
{
    size_t total = 0;
    const char* p = static_cast<const char*>(buf);
    while (total < len)
    {
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

UDSListener::UDSListener(const std::string& path) : path(path)
{
    unlink(path.c_str());
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0'; 

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        close(server_fd);
        server_fd = -1;
        return;
    }
    listen(server_fd, 10);
}

UDSListener::~UDSListener()
{
    Close();
}

int UDSListener::Accept()
{
    if (server_fd < 0) return -1;
    return accept(server_fd, nullptr, nullptr);
}

void UDSListener::Close()
{
    if (server_fd >= 0)
    {
        close(server_fd);
        server_fd = -1;
        unlink(path.c_str());
    }
}

UDS::UDS(int fd) : fd(fd) {}
UDS::~UDS()
{
    Close();
}

bool UDS::Read(std::string& out)
{
    if (fd < 0) return false;
    uint32_t len = 0;
    if (!read_exact(fd, &len, sizeof(len))) return false;
    if (len == 0 || len > 1024 * 1024) return false;

    out.resize(len);
    return read_exact(fd, out.data(), len);
}

bool UDS::Write(const std::string& data)
{
    if (fd < 0) return false;
    uint32_t len = static_cast<uint32_t>(data.size());
    if (!write_exact(fd, &len, sizeof(len))) return false;
    return write_exact(fd, data.data(), len);
}

void UDS::Close()
{
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        fd = -1;
    }
}
#include "UDS.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

static bool read_exact(int fd, void* buf, size_t len)
{
    size_t total = 0;
    char* p = static_cast<char*>(buf);
    while (total < len) {
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
        ssize_t n = write(fd, p + total, len - total);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

UDS::~UDS()
{
    Close();
}

bool UDS::Connect(const std::string& path, int timeout_ms)
{
    Close();
    auto start = std::chrono::steady_clock::now();

    while (true)
    {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0)
        {
            return true;
        }

        close(fd);
        fd = -1;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) return false;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
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
#include "Server.hpp"
#include <csignal>

int main()
{
    signal(SIGPIPE, SIG_IGN); 

    Server server;
    server.Run();
    return 0;
}
/*
 * main.cpp
 */

#pragma execution_character_set("utf-8")
#include "Server.hpp"


int main()
{
    // Encoding
    system("chcp 65001");
    // Running Server
    Server* server = new Server();
    server->Run();
    return 0;
}
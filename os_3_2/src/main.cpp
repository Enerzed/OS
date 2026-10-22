/*
 * main.cpp
 */

#pragma execution_character_set("utf-8")
#include "Client.hpp"

int main()
{
    system("chcp 65001");
    Client* client = new Client();
    client->Run();
    return 0;
}
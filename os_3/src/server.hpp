#pragma once

#define WIDTH 1024
#define HEIGHT 1024

#include "pipes.hpp"
#include "interface.hpp"

#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <vector>
#include <csignal>


class Server
{
private:
	sf::RenderWindow window(sf::VideoMode(sf::Vector2u(WIDTH, HEIGHT)), "Pipes Server", sf::Style::Close);
	sf::Time* time;								// Время SFML
	Interface* interface;						// Интерфейс
	std::vector<int> serverVector;				// Вектор для сигналов
public:
	Server();									// Инициализация окна времени и другого
	void Run();									// Основной цикл
	void RunNetwork();							// Обработка чатов
	void RunSystemMessages();					// Обработка системных сообщений
	void RunPackets();							// Обработка полученных пакетов
	void RunInterface();						// Обработка интерфейса
	void ServerDownHandler();					// Обработчик, если сервер был так или иначе выключен
	void AddServerVector(int);					// Добавления сигнала для обработки
};
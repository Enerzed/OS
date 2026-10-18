/*
 * Interface.hpp
 */

#pragma once

#include <SFML/Graphics.hpp>

#include <imgui-SFML.h>
#include <imgui.h>

#include <string>
#include <vector>

class Interface
{
public:
    void Init(sf::RenderWindow& window);
    void Shutdown();
    void Update(sf::RenderWindow& window, sf::Time time);
    void Render();

    void AddTextBox(unsigned short id, const std::string& title = "");
    void TextBoxMessage(const std::string& name, const std::string& msg, unsigned short id);
    void TextBoxSystemMessage(const std::string& msg, unsigned short id);

private:
    struct TextBox
    {
        std::string title;
        std::vector<std::string> messages;
        bool autoScroll = true;
    };

    void ensureBox(unsigned short id);
    void pushLine(unsigned short id, std::string line);

    std::vector<TextBox> boxes;
    bool initialized = false;
};
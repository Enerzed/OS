/*
 * Interface.hpp
 */

#pragma once

#include <SFML/Graphics.hpp>

#include <imgui-SFML.h>
#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

class Interface
{
public:
    using SendCallback = std::function<void(const std::string&)>;

    void Init(sf::RenderWindow& window);
    void Shutdown();
    void Update(sf::RenderWindow& window, sf::Time time);
    void Render();

    void AddTextBox(unsigned short id, const std::string& title = "");
    void TextBoxMessage(const std::string& name, const std::string& msg, unsigned short id);
    void TextBoxSystemMessage(const std::string& msg, unsigned short id);

    // Привязывает к боксу строку ввода (InputText + Send).
    // onSend вызывается при Enter или клике по кнопке с непустым текстом.
    void SetTextBoxInput(unsigned short id,
                         SendCallback onSend,
                         const char* hint = "Type a message...");

private:
    struct InputRow
    {
        bool active = false;
        SendCallback onSend;
        std::string hint = "Type a message...";
        char buffer[512] = {};
        bool refocus = false;
    };

    struct TextBox
    {
        std::string title;
        std::vector<std::string> messages;
        bool autoScroll = true;
        InputRow input;
    };

    void ensureBox(unsigned short id);
    void pushLine(unsigned short id, std::string line);
    void renderInputRow(size_t index, TextBox& box);

    std::vector<TextBox> boxes;
    bool initialized = false;
};
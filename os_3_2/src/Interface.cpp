/*
 * Interface.cpp
 */

#include "Interface.hpp"

#include <iostream>

namespace
{
    constexpr size_t MAX_LINES_PER_BOX = 2000;

    // Пропорциональный layout: 25% высоты сверху (log), остальное — chat.
    // Работает при любом размере окна, одинаково для сервера и клиента.
    struct Layout { ImVec2 pos; ImVec2 size; };

    Layout computeLayout(size_t index, size_t total)
    {
        const float margin = 8.0f;
        const float gap    = 8.0f;

        ImVec2 windowSize = ImGui::GetIO().DisplaySize;
        float usableW = windowSize.x - 2.0f * margin;
        float usableH = windowSize.y - 2.0f * margin - gap * (float)(total - 1);

        if (total == 2)
        {
            float topH = usableH * 0.25f;
            float botH = usableH - topH;
            if (index == 0) return { { margin, margin },                    { usableW, topH } };
            else            return { { margin, margin + topH + gap },       { usableW, botH } };
        }

        // fallback: делим поровну
        float each = usableH / (float)total;
        return {
            { margin, margin + (each + gap) * (float)index },
            { usableW, each }
        };
    }
}

void Interface::Init(sf::RenderWindow& window)
{
    if (initialized) return;
    if (!ImGui::SFML::Init(window))
        std::cerr << "[ui] ImGui::SFML::Init failed\n";
    initialized = true;
}

void Interface::Shutdown()
{
    if (!initialized) return;
    ImGui::SFML::Shutdown();
    initialized = false;
}

void Interface::Update(sf::RenderWindow& window, sf::Time time)
{
    ImGui::SFML::Update(window, time);
}

void Interface::ensureBox(unsigned short id)
{
    while (boxes.size() <= id)
    {
        TextBox tb;
        tb.title = "TextBox " + std::to_string(boxes.size());
        boxes.push_back(std::move(tb));
    }
}

void Interface::AddTextBox(unsigned short id, const std::string& title)
{
    ensureBox(id);
    if (!title.empty()) boxes[id].title = title;
}

void Interface::SetTextBoxInput(unsigned short id, SendCallback onSend, const char* hint)
{
    ensureBox(id);
    auto& box = boxes[id];
    box.input.active = true;
    box.input.onSend = std::move(onSend);
    if (hint) box.input.hint = hint;
    box.input.buffer[0] = '\0';
}

void Interface::pushLine(unsigned short id, std::string line)
{
    ensureBox(id);
    auto& box = boxes[id];
    box.messages.push_back(std::move(line));

    if (box.messages.size() > MAX_LINES_PER_BOX)
    {
        box.messages.erase(
            box.messages.begin(),
            box.messages.begin() + (box.messages.size() - MAX_LINES_PER_BOX));
    }
}

void Interface::TextBoxMessage(const std::string& name, const std::string& msg, unsigned short id)
{
    if (name.empty()) pushLine(id, msg);
    else              pushLine(id, name + ": " + msg);
}

void Interface::TextBoxSystemMessage(const std::string& msg, unsigned short id)
{
    pushLine(id, "[system] " + msg);
}

void Interface::renderInputRow(size_t index, TextBox& box)
{
    if (box.input.refocus)
    {
        ImGui::SetKeyboardFocusHere();
        box.input.refocus = false;
    }

    ImGui::SetNextItemWidth(-100.0f);
    bool submit = ImGui::InputTextWithHint(
        ("##input_" + std::to_string(index)).c_str(),
        box.input.hint.c_str(),
        box.input.buffer,
        sizeof(box.input.buffer),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (ImGui::Button(("Send##btn_" + std::to_string(index)).c_str()))
        submit = true;

    if (submit && box.input.buffer[0] != '\0')
    {
        if (box.input.onSend) box.input.onSend(std::string(box.input.buffer));
        box.input.buffer[0] = '\0';
        box.input.refocus = true;
    }
}

void Interface::Render()
{
    const size_t count = boxes.size();

    for (size_t i = 0; i < count; ++i)
    {
        auto& box = boxes[i];
        std::string title = box.title + "###box_" + std::to_string(i);

        Layout L = computeLayout(i, count);
        ImGui::SetNextWindowPos(L.pos);
        ImGui::SetNextWindowSize(L.size);

        if (ImGui::Begin(
                title.c_str(),
                nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            const float footerLine = ImGui::GetFrameHeightWithSpacing();
            const float inputBlock = box.input.active
                ? (footerLine + ImGui::GetStyle().ItemSpacing.y)
                : 0.0f;

            ImGui::BeginChild(
                ("##scroll_" + std::to_string(i)).c_str(),
                ImVec2(0, -footerLine - inputBlock),
                true);

            for (const auto& line : box.messages)
                ImGui::TextUnformatted(line.c_str());

            if (box.autoScroll &&
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::Checkbox(
                ("Auto-scroll##" + std::to_string(i)).c_str(),
                &box.autoScroll);
            ImGui::SameLine();
            if (ImGui::Button(("Clear##" + std::to_string(i)).c_str()))
                box.messages.clear();

            if (box.input.active)
                renderInputRow(i, box);
        }
        ImGui::End();
    }
}
/*
 * Interface.cpp
 */

#include "Interface.hpp"

#include <iostream>

namespace
{
    constexpr size_t MAX_LINES_PER_BOX = 2000;

    struct Layout { ImVec2 pos; ImVec2 size; };
    constexpr size_t LAYOUT_COUNT = 2;
    const Layout LAYOUTS[LAYOUT_COUNT] = {
        { ImVec2(8.0f,   8.0f), ImVec2(704.0f, 180.0f) }, // Server log
        { ImVec2(8.0f, 196.0f), ImVec2(704.0f, 516.0f) }, // Chat
    };
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
    if (name.empty())
        pushLine(id, msg);
    else
        pushLine(id, name + ": " + msg);
}

void Interface::TextBoxSystemMessage(const std::string& msg, unsigned short id)
{
    pushLine(id, "[system] " + msg);
}

void Interface::Render()
{
    const size_t count = boxes.size() < LAYOUT_COUNT ? boxes.size() : LAYOUT_COUNT;

    for (size_t i = 0; i < count; ++i)
    {
        auto& box = boxes[i];
        std::string title = box.title + "###box_" + std::to_string(i);

        // Фиксированные позиция и размер. NoMove/NoResize запрещают
        // пользователю перетаскивать и тянуть за угол.
        ImGui::SetNextWindowPos(LAYOUTS[i].pos);
        ImGui::SetNextWindowSize(LAYOUTS[i].size);

        if (ImGui::Begin(
                title.c_str(),
                nullptr,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            const float footer = ImGui::GetFrameHeightWithSpacing();
            ImGui::BeginChild(
                ("##scroll_" + std::to_string(i)).c_str(),
                ImVec2(0, -footer),
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
        }
        ImGui::End();
    }
}
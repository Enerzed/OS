#include "Page.hpp"
#include "imgui.h"
#include "imgui-SFML.h"
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <optional>
#include <set>

// Trim whitespace
static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Parse "7, 0, 1, ..." into a vector of ints
static std::vector<int> parseReference(const std::string& input) {
    std::vector<int> out;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        if (token.empty()) continue;
        try { out.push_back(std::stoi(token)); } catch (...) {}
    }
    return out;
}

int main() {
    sf::RenderWindow window(sf::VideoMode({1200, 780}), "Paging");
    window.setFramerateLimit(60);
    ImGui::SFML::Init(window);

    int        frames_cfg = 3;
    char       ref_cfg[512] = "3, 0, 7, 2, 5, 1, 6, 4, 2, 7, 0, 5, 3, 6, 1, 4, 7, 2, 0, 6, 4, 1, 7, 3, 5, 0, 2, 6, 1, 4, 7, 2, 0, 5, 3, 6, 1, 4, 7, 2, 5, 0, 6, 3, 1, 7, 4, 2, 0, 5, 1, 6, 4, 0, 7, 2, 3, 5, 6, 1, 2, 7, 0, 4, 5, 3, 6, 1, 2, 0, 6, 3, 1, 5, 7, 4, 0, 2, 6, 3, 4, 0, 7, 1, 5, 2, 6, 3, 4, 7, 1, 5, 0, 6, 2, 4, 7, 3, 5, 1";
    float      speed_cfg  = 0.8f;

    PageManager pm(frames_cfg);
    std::vector<int> reference;
    size_t   idx          = 0;
    bool     auto_play    = false;
    float    step_timer   = 0.0f;

    int   anim_victim_page  = -1;
    int   anim_victim_frame = -1;
    float anim_timer        = 0.0f;
    const float ANIM_DUR    = 3.0f;

    int total_faults = 0;
    int total_hits   = 0;
    std::vector<std::string> action_log;

    bool needs_reset = true;

    auto resetSim = [&]() {
        pm = PageManager(frames_cfg);
        reference = parseReference(ref_cfg);
        idx = 0;
        auto_play = false;
        step_timer = 0.0f;
        anim_victim_page = -1;
        anim_victim_frame = -1;
        anim_timer = 0.0f;
        total_faults = total_hits = 0;
        action_log.clear();
        action_log.push_back("Simulation reset. Press Next Step or enable Auto Play.");
        needs_reset = false;
    };

    auto doStep = [&]() {
        if (idx >= reference.size()) return;
        const int page = reference[idx];
        pm.accessPage(page);
        idx++;

        if (pm.wasLastHit()) {
            total_hits++;
            action_log.push_back("HIT   : page " + std::to_string(page) + " already in RAM");
        } else if (pm.wasLastFault()) {
            total_faults++;
            std::string msg = "FAULT : load page " + std::to_string(page);
            if (pm.getLastVictimPage() != -1) {
                msg += "  |  evict page " + std::to_string(pm.getLastVictimPage()) +
                       " (frame " + std::to_string(pm.getLastVictimFrame()) + ") -> DISK";
                anim_victim_page  = pm.getLastVictimPage();
                anim_victim_frame = pm.getLastVictimFrame();
                anim_timer        = ANIM_DUR;
            } else if (pm.wasLastLoadedFromDisk()) {
                msg += "  |  from DISK";
            }
            action_log.push_back(msg);
        }

        if (action_log.size() > 200) action_log.erase(action_log.begin());
    };

    sf::Clock frame_clock;

    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);
            if (event->is<sf::Event::Closed>()) window.close();
        }

        sf::Time dt = frame_clock.restart();
        float dt_sec = dt.asSeconds();
        ImGui::SFML::Update(window, dt);

        if (anim_timer > 0.0f) anim_timer = std::max(0.0f, anim_timer - dt_sec);
        if (needs_reset) resetSim();

        const ImGuiWindowFlags kFixed =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse;

        ImGui::SetNextWindowPos(ImVec2(10, 10),  ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(580, 170), ImGuiCond_Always);
        ImGui::Begin("Configuration", nullptr, kFixed);
        ImGui::SetNextItemWidth(180);
        bool frames_changed = ImGui::SliderInt("Frames (RAM size)", &frames_cfg, 1, 8);
        ImGui::SetNextItemWidth(420);
        bool ref_changed = ImGui::InputText("Reference String", ref_cfg, sizeof(ref_cfg));
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Auto-Play Speed (s)", &speed_cfg, 0.1f, 3.0f, "%.1f");
        if (frames_changed || ref_changed) needs_reset = true;
        if (ImGui::Button("Reset Simulation")) needs_reset = true;
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(600, 10),  ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(590, 170), ImGuiCond_Always);
        ImGui::Begin("Controls", nullptr, kFixed);
        if (ImGui::Button("Next Step")) doStep();
        ImGui::SameLine();
        ImGui::Checkbox("Auto Play", &auto_play);
        ImGui::SameLine();
        if (ImGui::Button("Jump to End")) {
            while (idx < reference.size()) doStep();
        }

        if (auto_play && idx < reference.size()) {
            step_timer += dt_sec;
            if (step_timer >= speed_cfg) { doStep(); step_timer = 0.0f; }
        }

        ImGui::Separator();
        int accesses = total_faults + total_hits;
        float hit_rate = accesses > 0 ? 100.0f * total_hits / accesses : 0.0f;
        ImGui::Text("Accesses: %d   Faults: %d   Hits: %d   Hit Rate: %.1f%%",
                    accesses, total_faults, total_hits, hit_rate);
        ImGui::Text("Progress: %zu / %zu", idx, reference.size());

        ImGui::Text("Reference:");
        ImGui::SameLine();
        for (size_t i = 0; i < reference.size(); ++i) {
            if (i == idx) ImGui::TextColored(ImVec4(1, 1, 0, 1), "[%d]", reference[i]);
            else          ImGui::Text("%d", reference[i]);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 190),  ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(285, 320), ImGuiCond_Always);
        ImGui::Begin("RAM - Physical Frames", nullptr, kFixed);
        {
            const auto& frames = pm.getFrames();
            for (int i = 0; i < pm.getCapacity(); ++i) {
                bool is_victim = (anim_timer > 0.0f && i == anim_victim_frame);

                ImVec4 col;
                if (frames[i] == -1) col = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
                else                 col = ImVec4(0.35f, 1.00f, 0.35f, 1.0f);

                if (is_victim) {
                    float t = anim_timer / ANIM_DUR;   // 1 -> 0
                    col = ImVec4(1.0f,
                                 0.35f + 0.65f * (1 - t),
                                 0.20f + 0.80f * (1 - t), 1.0f);
                }

                if (frames[i] == -1)
                    ImGui::TextColored(col, "Frame %d: [empty]", i);
                else
                    ImGui::TextColored(col, "Frame %d: Page %d", i, frames[i]);

                if (is_victim) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.1f, 1.0f),
                                       "<== swapping out to disk");
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(305, 190),  ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(285, 320), ImGuiCond_Always);
        ImGui::Begin("Disk - Swapped-out Pages", nullptr, kFixed);
        {
            const auto& disk = pm.getDiskPages();
            if (disk.empty()) {
                ImGui::TextDisabled("(disk empty)");
            } else {
                for (int p : disk) {
                    bool just_evicted = (anim_timer > 0.0f && p == anim_victim_page);
                    if (just_evicted)
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.1f, 1.0f), "Page %d   <- just written to disk", p);
                    else
                        ImGui::Text("Page %d", p);
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(600, 190),  ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(590, 320), ImGuiCond_Always);
        ImGui::Begin("FIFO Queue", nullptr, kFixed);
        {
            std::queue<int> qc = pm.getQueue();
            ImGui::Text("Head ->");
            ImGui::SameLine();
            while (!qc.empty()) {
                ImGui::Button(("Page " + std::to_string(qc.front())).c_str());
                ImGui::SameLine();
                qc.pop();
            }
            ImGui::Text("<- Tail");
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 520),   ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1180, 250), ImGuiCond_Always);
        ImGui::Begin("Action Log", nullptr, kFixed);
        for (const auto& line : action_log) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::End();

        window.clear(sf::Color(30, 30, 30));
        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}
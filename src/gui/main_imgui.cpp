// LoliProfilerImGui - minimal Dear ImGui + SFML application shell.
// This is the entry point for the experimental ImGui-based GUI.
// No Qt includes here; Qt5::Core/Qt5::Network are linked for later use.

#include "imgui.h"
#include "imgui-SFML.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>

int main()
{
    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;

    sf::RenderWindow window(sf::VideoMode({1280, 720}), "LoliProfiler", sf::Style::Default, settings);
    window.setVerticalSyncEnabled(true);

    if (!ImGui::SFML::Init(window))
        return -1;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    sf::Clock deltaClock;
    while (window.isOpen())
    {
        while (const auto event = window.pollEvent())
        {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        // Root dockspace over the whole viewport
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        // Menu bar
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Run/Launch...", nullptr, false, false))
                {
                    // TODO: launch profiling session
                }
                if (ImGui::MenuItem("Open Record...", nullptr, false, false))
                {
                    // TODO: open .loli record via nfd
                }
                if (ImGui::BeginMenu("Themes"))
                {
                    if (ImGui::MenuItem("Dark", nullptr, false, false))
                    {
                        // TODO: apply dark theme
                    }
                    if (ImGui::MenuItem("Forest Green", nullptr, false, false))
                    {
                        // TODO: apply forest green theme
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    window.close();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Placeholder dockable window
        ImGui::Begin("Capture");
        ImGui::Text("LoliProfiler ImGui shell - panels coming soon");
        ImGui::End();

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}

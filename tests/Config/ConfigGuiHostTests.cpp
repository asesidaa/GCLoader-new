#include "Win32D3D11Host.h"

#include "imgui.h"

#include <Windows.h>

#include <iostream>

int main()
{
    Win32D3D11Host host;
    const auto opened = host.Open(GetModuleHandleW(nullptr), nullptr, nullptr);
    if (!opened)
    {
        std::cerr << "Failed to open the production ConfigGUI host: "
                  << opened.error() << '\n';
        return 1;
    }

    if (ImGui::GetCurrentContext() == nullptr)
    {
        std::cerr << "ConfigGUI host did not create an ImGui context\n";
        return 1;
    }
    if (ImGui::GetIO().IniFilename != nullptr)
    {
        std::cerr << "ConfigGUI host still enables imgui.ini persistence\n";
        return 1;
    }

    host.Close();
    return 0;
}

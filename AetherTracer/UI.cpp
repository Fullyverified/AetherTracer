#include "UI.h"
#include "Imgui.h"
#include "Config.h"


bool UI::isWindowHovered = false;
uint32_t UI::raysPerSecond = 0;
float UI::frameTime = 0;
uint32_t UI::numRays = 0;
bool UI::accumulate = config.accumulate;

void UI::renderSettings() {

    isWindowHovered = ImGui::GetIO().WantCaptureMouse ? true : false;

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);

    ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_None);

    raysPerSecond = (config.resX * config.resY) * (1000.0f / frameTime);
    ImGui::Text("Rays /s: %d", raysPerSecond);

    std::string frameTimeStr = std::to_string(static_cast<int>(frameTime)) + " ms";
    ImGui::Text("Frame Time: %s", frameTimeStr.c_str());

    ImGui::Text("Num Rays: %d", numRays);

    ImGui::Checkbox("Accumulate", &accumulate);

    ImGui::End();

}
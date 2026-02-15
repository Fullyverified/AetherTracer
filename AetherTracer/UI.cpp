#include "UI.h"

#include "Imgui.h"


void UI::renderSettings() {

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);

    ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_None);

    ImGui::Text("Rays /s: %d");

    ImGui::End();

}
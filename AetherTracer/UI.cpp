#include "UI.h"
#include "Imgui.h"

#include <string>
#include <limits>

#include "MeshManager.h"
#include "EntityManager.h"
#include "MaterialManager.h"

MeshManager* UI::meshManager = nullptr;
EntityManager* UI::entityManager = nullptr;
MaterialManager* UI::materialManager = nullptr;

bool UI::isWindowHovered = false;
bool UI::accelUpdate = false; // reset by the renderer
bool UI::accumulationUpdate = false; // reset by the renderer
bool UI::materialUpdate = false; // reset by the renderer
bool UI::renderUI = true;

uint64_t UI::raysPerSecond = 0;
float UI::frameTime = 0;
uint32_t UI::numRays = 0;

int UI::current_tone_mapper = static_cast<int>(config.tone_mapper);

PT::Vector3 UI::camera_position = {};
PT::Vector2 UI::camera_rotation = {};

void UI::renderSettings() {

    isWindowHovered = ImGui::GetIO().WantCaptureMouse ? true : false;

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);

    ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_None);

    raysPerSecond = static_cast<uint64_t>((config.resX * config.resY) * (1000.0f / frameTime));
    ImGui::Text("Rays /s: %u", raysPerSecond);

    std::string frameTimeStr = std::to_string(static_cast<uint64_t>(frameTime * 1000)) + " ms";
    ImGui::Text("Frame Time: %s", frameTimeStr.c_str());

    ImGui::Text("Num Rays: %d", numRays);

    ImGui::Checkbox("Accumulate", &config.accumulate);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::SliderInt("##Samples Per Pixel", &config.raysPerPixel, 1, 50, "Samples Per Pixel %i")) {
        accumulationUpdate = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::SliderInt("##Min ray bounces", &config.minBounces, 0, config.minBouncesMax, "Min Bounces %i")) {
        if (config.maxBounces < config.minBounces) {
            config.maxBounces = config.minBounces;
            config.maxBounces = config.maxBounces;
        }
        accumulationUpdate = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::SliderInt("##Max ray bounces", &config.maxBounces, 0, config.maxBouncesMax, "Max bounces %i")) {
        if (config.maxBounces < config.minBounces) {
            config.maxBounces = config.minBounces;
        }
        accumulationUpdate = true;
    }

    if (ImGui::Checkbox("Next Event Estimation", &config.NEE)) {
        accumulationUpdate = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::SliderInt("##NEE Candidate Samples", &config.NEE_samples, 1, 50, "Candidate Samples % i")) {
        accumulationUpdate = true;
    }

    if (ImGui::Checkbox("Jitter", &config.jitter)) {
        accumulationUpdate = true;
    }

    // Drop down list of tone mappers

    const char* tone_mappers[] = { "Reinhard Extended", "Hable Filmic", "Aces Filmic" };

    ImGui::Text("Tone Mapping: ");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);


    ImGui::DragFloat("##White Point", &config.whitepoint, 0.05f, 0.001f, std::numeric_limits<float>::max(), "White Point: %.3f");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::Combo("##Tone Mapping", &current_tone_mapper, tone_mappers, static_cast<int>(Tone_Mapper::Count))) {
        config.tone_mapper = static_cast<Tone_Mapper>(current_tone_mapper);
    }

    // Camera

    // Position

    ImGui::Text("Translation: ");
    camera_position = entityManager->camera->position;

    // X Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position X", &camera_position.x, 0.1f, 0.0f, 0.0f, "X %.3f")) {

        entityManager->camera->position = camera_position;
        accumulationUpdate = true;
    }

    // Y Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position Y", &camera_position.y, 0.1f, 0.0f, 0.0f, "Y %.3f")) {
        entityManager->camera->position = camera_position;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Z Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position Z", &camera_position.z, 0.1f, 0.0f, 0.0f, "Z %.3f")) {
        entityManager->camera->position = camera_position;
        accumulationUpdate = true;
    }

    // Rotation

    ImGui::Text("Rotation: ");
    camera_rotation = entityManager->camera->rotation;

    // X Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Rotation X", &camera_rotation.x, 0.1f, 0.0f, 0.0f, "X %.3f")) {

        entityManager->camera->rotation = camera_rotation;
        accumulationUpdate = true;
    }

    // Y Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Rotation Y", &camera_rotation.y, 0.1f, 0.0f, 0.0f, "Y %.3f")) {
        entityManager->camera->rotation = camera_rotation;
        accumulationUpdate = true;
    }

    ImGui::End();

}

std::vector<const char*> UI::models;
int UI::mesh_selection_idx = 0;
std::vector<const char*> UI::entity;
int UI::entity_selection_idx = 0;
int UI::renaming_index_entity = -1;
int UI::deleting_index_entity = -1;
char UI::renaming_buffer_entity[128] = "";

PT::Vector3 UI::entity_position = {};
PT::Vector3 UI::entity_rotation = {};
PT::Vector3 UI::entity_scale = {};

std::vector<const char*> UI::materials;
int UI::material_selection_idx = 0;
int UI::entity_material_idx = -1;

void UI::sceneEditor() {
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);

    ImGui::Begin("Scene Editor", nullptr, ImGuiWindowFlags_None);

    if (ImGui::Checkbox("Sky", &config.sky)) {
        accumulationUpdate = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Save Level")) {
    
        entityManager->saveScene("default_scene");
    
    }

    // Drop down to select an existant Entity

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::BeginListBox("##entity", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12))) {
        for (int i = 0; i < UI::entity.size(); i++) {
            ImGui::PushID(i);

            const bool is_selected = (entity_selection_idx == i);


            if (i != renaming_index_entity && i != deleting_index_entity) {
            
                if (ImGui::Selectable(UI::entity[i], is_selected)) {

                    renaming_index_entity = - 1;
                    deleting_index_entity = -1;
                    entity_selection_idx = i;

                    for (size_t j = 0; j < UI::materials.size(); j++) {
                    
                        if (entityManager->entities[entity_selection_idx]->material == UI::materials[j]) {
                            entity_material_idx = j;
                        }

                    }

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { // not working
                        renaming_index_entity = -1;  // exit rename mode
                        ImGui::SetNextFrameWantCaptureKeyboard(false);

                    }

                }

                
            }

            if (i != renaming_index_entity) {

                // double click - renaming
                if (is_selected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {

                    renaming_index_entity = i;
                    deleting_index_entity = -1;

                    strncpy_s(renaming_buffer_entity, UI::entity[i], sizeof(renaming_buffer_entity) - 1);
                }

            }

            if (i != deleting_index_entity) {

                // right click - deleting
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    deleting_index_entity = i;
                    renaming_index_entity = -1;
                }

            }

            if (renaming_index_entity == i) {

                 ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

                 if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

                 if (ImGui::InputText("##Rename", renaming_buffer_entity, sizeof(renaming_buffer_entity), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                     
                     entityManager->entities[i]->name = renaming_buffer_entity;
                     updateUIentity();


                     if (ImGui::IsKeyPressed(ImGuiKey_Enter) || !ImGui::IsItemFocused() || ImGui::IsItemDeactivated()) {
                         renaming_index_entity = -1;  // exit rename mode
                         ImGui::SetNextFrameWantCaptureKeyboard(false);
                     }

                 }

            }

            if (deleting_index_entity == i) {

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

                if (ImGui::Button("Delete")) {

                    entityManager->deleteEntity(i);
                    updateUIentity();
                    accumulationUpdate = true;
                    accelUpdate = true;
                    materialUpdate = true;

                    if (ImGui::IsItemDeactivated() || !ImGui::IsItemActive() || !ImGui::IsItemFocused() || !ImGui::IsAnyItemFocused()) {
                        deleting_index_entity = -1;  // exit deleting mode
                        ImGui::SetNextFrameWantCaptureKeyboard(false);

                    }

                }

            }

            ImGui::PopID();

        }
        ImGui::EndListBox();

    }


    // Change material

    ImGui::Text("Entity Material: ");

    ImGui::SameLine();

    if (ImGui::Combo("##Materials", &entity_material_idx, UI::materials.data(), static_cast<int>(UI::materials.size()))) {

        entityManager->entities[entity_selection_idx]->material = UI::materials[entity_material_idx];
        accumulationUpdate = true;
        materialUpdate = true;
    }

    // Position

    ImGui::Text("Translation: ");
    entity_position = entityManager->entities[entity_selection_idx]->position;

    // X Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position X", &entity_position.x, 0.1f, 0.0f, 0.0f, "X %.3f")) {

        entityManager->entities[entity_selection_idx]->position = entity_position;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Y Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position Y", &entity_position.y, 0.1f, 0.0f, 0.0f, "Y %.3f")) {
        entityManager->entities[entity_selection_idx]->position = entity_position;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Z Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Position Z", &entity_position.z, 0.1f, 0.0f, 0.0f, "Z %.3f")) {
        entityManager->entities[entity_selection_idx]->position = entity_position;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Rotation

    ImGui::Text("Rotation: ");
    entity_rotation = entityManager->entities[entity_selection_idx]->rotation;

    // X Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Rotation X", &entity_rotation.x, 0.1f, 0.0f, 0.0f, "X %.3f")) {

        entityManager->entities[entity_selection_idx]->rotation = entity_rotation;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Y Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Rotation Y", &entity_rotation.y, 0.1f, 0.0f, 0.0f, "Y %.3f")) {
        entityManager->entities[entity_selection_idx]->rotation = entity_rotation;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Z Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##rotation Z", &entity_rotation.z, 0.1f, 0.0f, 0.0f, "Z %.3f")) {
        entityManager->entities[entity_selection_idx]->rotation = entity_rotation;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Scale

    ImGui::Text("Scale: ");
    entity_scale = entityManager->entities[entity_selection_idx]->scale;

    // X Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Scale X", &entity_scale.x, 0.1f, 0.0f, 0.0f, "X %.3f")) {

        entityManager->entities[entity_selection_idx]->scale = entity_scale;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Y Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Scale Y", &entity_scale.y, 0.1f, 0.0f, 0.0f, "Y %.3f")) {
        entityManager->entities[entity_selection_idx]->scale = entity_scale;
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Z Axis
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::DragFloat("##Scale Z", &entity_scale.z, 0.1f, 0.0f, 0.0f, "Z %.3f")) {
        entityManager->entities[entity_selection_idx]->scale = entity_scale;
        accumulationUpdate = true;
        accelUpdate = true;
    }
 
    if (ImGui::Button("Add Entity")) {

        entityManager->addEntity(UI::models[mesh_selection_idx], UI::models[mesh_selection_idx]);
        updateUIentity();
        accumulationUpdate = true;
        accelUpdate = true;
        materialUpdate = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Swap Model")) {

        entityManager->swapEntityModel(UI::models[mesh_selection_idx], UI::entity_selection_idx);
        updateUIentity();
        accumulationUpdate = true;
        accelUpdate = true;
    }

    // Drop down to select a mesh Type
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    ImGui::Combo("##Mesh Objects", &mesh_selection_idx, UI::models.data(), static_cast<int>(UI::models.size()));


    ImGui::End();
}

int UI::renaming_index_material = -1;
char UI::renaming_buffer_material[128] = "";

PT::Vector3 UI::color = {};
float UI::roughness = 1.0f;
float UI::metallic = 0.0f;
float UI::IOR = 1.0f;
float UI::transmission = 0.0f;
float UI::emission = 0.0f;

void UI::materialEditor() {
    MaterialManager::Material* selected_material;

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);

    ImGui::Begin("Material Editor", nullptr, ImGuiWindowFlags_None);

    if (ImGui::Button("Save Materials")) {
        materialManager->saveMaterials("default_materials");
    }

    // Drop down to select an Material

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); // Set width to the available space
    if (ImGui::BeginListBox("##Materials", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8))) {
        for (int i = 0; i < UI::materials.size(); i++) {
            ImGui::PushID(i);

            const bool is_selected = (material_selection_idx == i);

            if (i != renaming_index_material) {

                if (ImGui::Selectable(UI::materials[i], is_selected)) {

                    material_selection_idx = i;
                   

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { // not working
                        renaming_index_material = -1;  // exit rename mode
                        ImGui::SetNextFrameWantCaptureKeyboard(false);

                    }

                }

                // double click
                if (is_selected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renaming_index_material = i;

                    strncpy_s(renaming_buffer_material, UI::materials[i], sizeof(renaming_buffer_material) - 1);
                }
            }


            if (renaming_index_material == i) {

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

                if (ImGui::InputText("##Rename", renaming_buffer_material, sizeof(renaming_buffer_material), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {

                    MaterialManager::Material* material = materialManager->materials[UI::materials[i]];
                    materialManager->materials.erase(UI::materials[i]);

                    material->name = renaming_buffer_material;
                    materialManager->materials[renaming_buffer_material] = material;

                    updateUIMaterials();


                    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || !ImGui::IsItemFocused()) {
                        renaming_index_material = -1;  // exit rename mode
                        ImGui::SetNextFrameWantCaptureKeyboard(false);

                    }

                }

            }

            ImGui::PopID();

        }
        ImGui::EndListBox();

    }


    if (material_selection_idx != -1) {
    
        selected_material = materialManager->materials[materials[material_selection_idx]];

        if (selected_material != nullptr) {
        
            color = selected_material->color;
            roughness = selected_material->roughness;
            metallic = selected_material->metallic;
            IOR = selected_material->ior;
            transmission = selected_material->transmission;
            emission = selected_material->emission;
        
        
        }

        // Colour Editing
        float colourArray[3] = { color.x, color.y, color.z };

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::ColorPicker3("##Colour", colourArray, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoSidePreview)) {
            // Update the Vector3 with the new color values
            color.x = colourArray[0];
            color.y = colourArray[1];
            color.z = colourArray[2];
            selected_material->color = { color.x, color.y, color.z };
            accumulationUpdate = true;
            materialUpdate = true;
        }

        // General Material Editing

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##Roughness", &roughness, 0.025f, 0.0f, 1.0f, "Roughness %.3f")) {
            roughness = roughness > 1 ? 1 : roughness;
            roughness = roughness < 0 ? 0 : roughness;
            selected_material->roughness = roughness;
            accumulationUpdate = true;
            materialUpdate = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##Metallic", &metallic, 0.025f, 0.0f, 1.0f, "Metallic %.3f")) {
            metallic = metallic > 1 ? 1 : metallic;
            metallic = metallic < 0 ? 0 : metallic;
            selected_material->metallic = metallic;
            accumulationUpdate = true;
            materialUpdate = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##IOR", &IOR, 0.025f, 1.0f, 10.0f, "IOR %.3f")) {
            IOR = IOR < 1 ? 1 : IOR;
            selected_material->ior = IOR;
            accumulationUpdate = true;
            materialUpdate = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##Transmission", &transmission, 0.025f, 0.0f, 1.0f, "Transmission %.3f")) {
            transmission = transmission > 1 ? 1 : transmission;
            transmission = transmission < 0 ? 0 : transmission;
            selected_material->transmission = transmission;
            accumulationUpdate = true;
            materialUpdate = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##Emission", &emission, 0.025f, 0.0f, 20.0f, "Emission %.3f")) {
            emission = emission < 0 ? 0 : emission;
            selected_material->emission = emission;
            accumulationUpdate = true;
            materialUpdate = true;
        }

        if (ImGui::Button("New Material")) {
            materialManager->createMaterial();
            materialUpdate = true;
            UI::updateUIMaterials();
            material_selection_idx = UI::materials.size() - 1;

        }
    
    }


    ImGui::End();
}

void UI::updateUIModels() {

    UI::models.clear();

    for (size_t i = 0; i < meshManager->models.size(); i++) {
        UI::models.push_back(meshManager->models[i].c_str());
    }

}

void UI::updateUIentity() {

    UI::entity.clear();

    for (size_t i = 0; i < entityManager->entities.size(); i++) {
        UI::entity.push_back(entityManager->entities[i]->name.c_str());
    }

}

void UI::updateUIMaterials() {

    UI::materials.clear();

    for (auto& pair : materialManager->materials) {
    
        UI::materials.push_back(pair.first.c_str());

    }

}
#pragma once

#include "DX12Renderer.h"
#include "EntityManager.h"
#include "MeshManager.h"
#include "MaterialManager.h"
#include "Window.h"
#include "Config.h"
#include "UI.h"

#include "Imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx12.h"


class AetherTracer {

public:

	AetherTracer() {}
	~AetherTracer() {}

	void run() {

		auto window = new Window{};
		//auto window = Window("test", 1200, 1200);
		auto meshManager = new MeshManager();
		auto materialManager = new MaterialManager();
		auto entityManager = new EntityManager(materialManager);

		meshManager->initMeshes();
		materialManager->initDefaultMaterials();
		entityManager->initScene();

		auto* dx12renderer = new DX12Renderer{ entityManager, meshManager, materialManager, window };

		dx12renderer->init();

		while (!window->shouldClose()) {
			window->pollEvents();

			//ImGui_ImplDX12_NewFrame();
			//ImGui_ImplSDL3_NewFrame();
			//ImGui::NewFrame();

			//UI::renderSettings();

			// physics
			// rebuild bvh

			if (window->wasResized()) {
				dx12renderer->resize();
				window->acknowledgeResize();
			}

			dx12renderer->render();
			dx12renderer->present();
		}
	
	}
};
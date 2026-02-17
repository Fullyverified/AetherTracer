#pragma once

#include "DX12Renderer.h"
#include "EntityManager.h"
#include "MeshManager.h"
#include "MaterialManager.h"
#include "Window.h"
#include "Config.h"
#include "UI.h"
#include "InputManager.h"

#include <SDL3/SDL.h>
#include "Imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx12.h"
#include <chrono>
#include <cstdint>


class AetherTracer {

public:

	AetherTracer() {}
	~AetherTracer() {}

	void updateConfig() {
		config.accumulate = UI::accumulate;
	}

	void run() {

		auto meshManager = new MeshManager();
		auto materialManager = new MaterialManager();
		auto entityManager = new EntityManager(materialManager);
		auto inputManager = new InputManager();
		auto window = new Window{ "Aether Tracer", 1200, 1200 };


		meshManager->initMeshes();
		materialManager->initDefaultMaterials();
		entityManager->initScene();


		auto* dx12renderer = new DX12Renderer{ entityManager, meshManager, materialManager, window };


		dx12renderer->init();
		UI::numRays = 0;
		SDL_Event event;

		auto frameStartTime = std::chrono::high_resolution_clock::now();
		auto physicsTime = std::chrono::high_resolution_clock::now();

		std::chrono::milliseconds frameEndTime;
		while (!window->shouldClose()) {
			frameStartTime = std::chrono::high_resolution_clock::now();

			auto deltaTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - physicsTime);
			while (SDL_PollEvent(&event)) {
				ImGui_ImplSDL3_ProcessEvent(&event);
				
				// handle input
				window->pollEvents(event);
			}

			inputManager->processInput(event);
			inputManager->processInputContinuous(event, std::chrono::duration<float>(deltaTime).count(), entityManager->camera);
			window->setRelativeMouse(inputManager->lockMouse);
			// physics
			// rebuild bvh

			physicsTime = std::chrono::high_resolution_clock::now();
			updateConfig();

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();

			UI::renderSettings();

			if (window->wasResized()) {
				dx12renderer->resize();
				window->acknowledgeResize();
			}

			dx12renderer->render();
			dx12renderer->present();


			frameEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - frameStartTime);
			UI::frameTime = std::chrono::duration<float>(frameEndTime).count() * 1000;
			UI::numRays = config.accumulate ? ++UI::numRays : 1;
			entityManager->camera->camMoved = false;
		}
	
	}
};
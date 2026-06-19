#include "AetherTracer.h"

#include "Renderer.h"
#include "DX12Renderer.h"
#include "CUDARenderer.h"
#include "EntityManager.h"
#include "MeshManager.h"
#include "MaterialManager.h"
#include "Window.h"
#include "InputManager.h"
#include "UI.h"
#include "Config.h"

void AetherTracer::run() {

	init();

	auto frameStartTime = std::chrono::high_resolution_clock::now();
	auto physicsTime = std::chrono::high_resolution_clock::now();
	std::chrono::microseconds frameEndTime;

	SDL_Event event;
	while (!window->shouldClose() && running) {
		frameStartTime = std::chrono::high_resolution_clock::now();

		auto deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - physicsTime);
		physicsTime = std::chrono::high_resolution_clock::now();

		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);

			// handle input
			window->pollEvents(event);
			inputManager->processInput(event);

			if (window->wasResized()) {

				renderer->resize();
				window->acknowledgeResize();
			}

		}
		inputManager->processInputContinuous(event, std::chrono::duration<double>(deltaTime).count());

		// physics
		// rebuild bvh

		updateConfig();

		renderImgui();


		renderer->render();
		renderer->present();

		
		frameEndTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - frameStartTime);
		UI::frameTime = std::chrono::duration<float>(frameEndTime).count();
		UI::numRays = config.accumulate && !entityManager->camera->camMoved ? UI::numRays + config.raysPerPixel : config.raysPerPixel;
		entityManager->camera->camMoved = false;
		UI::accelUpdate = false;
		UI::accumulationUpdate = false;
	}

}

void AetherTracer::updateConfig() {
	//config.accumulate = UI::accumulate;
}

void AetherTracer::renderImgui() {
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
	
	if (UI::renderUI) {
		UI::renderSettings();
		UI::sceneEditor();
		UI::materialEditor();
	}
}

void AetherTracer::init() {

	meshManager = new MeshManager();
	materialManager = new MaterialManager();
	entityManager = new EntityManager(materialManager);
	inputManager = new InputManager(this);
	window = new Window{ "Aether Tracer", config.resX, config.resY };

	meshManager->initMeshes();
	materialManager->initDefaultMaterials();
	entityManager->initScene();

	if (config.gfx_api == DX12) {
		renderer = new DX12Renderer{ entityManager, meshManager, materialManager, window };
	}
	else if (config.gfx_api == CUDA) {
		renderer = new CUDARenderer{ entityManager, meshManager, materialManager, window };
	}

	renderer->init();

	UI::meshManager = meshManager;
	UI::entityManager = entityManager;
	UI::materialManager = materialManager;
	UI::numRays = 0;
	UI::updateUIModels();
	UI::updateUIentity();
	UI::updateUIMaterials();

}
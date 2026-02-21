#include "AetherTracer.h"

#include "DX12Renderer.h"
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
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);

			// handle input
			window->pollEvents(event);
			inputManager->processInput(event);
		}
		inputManager->processInputContinuous(event, std::chrono::duration<double>(deltaTime).count());

		// physics
		// rebuild bvh

		physicsTime = std::chrono::high_resolution_clock::now();
		updateConfig();

		renderImgui();

		if (window->wasResized()) {
			dx12Renderer->resize();
			window->acknowledgeResize();
		}

		dx12Renderer->render();
		dx12Renderer->present();


		frameEndTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - frameStartTime);
		UI::frameTime = std::chrono::duration<float>(frameEndTime).count();
		UI::numRays = config.accumulate && !entityManager->camera->camMoved ? UI::numRays + config.raysPerPixel: 1;
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
	UI::renderSettings();
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


	dx12Renderer = new DX12Renderer{ entityManager, meshManager, materialManager, window };

	dx12Renderer->init();
	UI::numRays = 0;
}
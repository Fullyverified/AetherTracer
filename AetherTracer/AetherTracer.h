#pragma once

#include <SDL3/SDL.h>
#include "Imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_dx12.h"
#include <chrono>
#include <cstdint>

class InputManager;
class Window;
class MaterialManager;
class MeshManager;
class EntityManager;
class DX12Renderer;

class AetherTracer {

public:

	AetherTracer() {}
	~AetherTracer() {}

	void init();
	void updateConfig();

	void renderImgui();

	void run();

	MeshManager* meshManager;
	MaterialManager* materialManager;
	EntityManager* entityManager;
	InputManager* inputManager;
	Window* window;
	DX12Renderer* dx12Renderer;
};
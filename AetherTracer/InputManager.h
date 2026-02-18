#pragma once

#include <iostream>
#include <SDL3/SDL.h>
#include "Vector.h"
#include "UI.h"

class AetherTracer;
class Window;
class MaterialManager;
class MeshManager;
class EntityManager;
class DX12Renderer;

class InputManager {
public:

	InputManager(AetherTracer* aetherTracer);
	~InputManager();


	void processInput(SDL_Event& event);
	void processInputContinuous(SDL_Event& event, float deltaTime);

	const bool* keys = SDL_GetKeyboardState(NULL);
	float mouseX = 0;
	float mouseY = 0;
	bool lockMouse = false;
	AetherTracer* aetherTracer;
};


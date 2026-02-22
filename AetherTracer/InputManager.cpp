#include "InputManager.h"

#include "DX12Renderer.h"
#include "EntityManager.h"
#include "MeshManager.h"
#include "MaterialManager.h"
#include "Window.h"
#include "AetherTracer.h"
#include "UI.h"
#include "Config.h"

InputManager::InputManager(AetherTracer* aetherTracer) : aetherTracer(aetherTracer) {};
InputManager::~InputManager() {};

void InputManager::processInput(SDL_Event& event) {
	

	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {

		if (!UI::isWindowHovered) {
			// find clicked object in scene
		}

	}

	if (event.type == SDL_EVENT_KEY_DOWN) {

		if (event.key.scancode == SDL_SCANCODE_DELETE) {
			lockMouse = lockMouse == true ? false : true;
			aetherTracer->window->setRelativeMouse(aetherTracer->inputManager->lockMouse);
		}
		if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
			aetherTracer->running = false;
		}
		if (event.key.scancode == SDL_SCANCODE_F1) {
			UI::renderUI = UI::renderUI ? false : true;
		}
	}


}

void InputManager::processInputContinuous(SDL_Event& event, float deltaTime) {

	PT::Vector2 mousePos = { event.button.x, event.button.y };

	// Continuous Input
	SDL_GetRelativeMouseState(&mouseX, &mouseY);

	if (keys[SDL_SCANCODE_W]) {
		aetherTracer->entityManager->camera->moveForward(deltaTime);
	}

	if (keys[SDL_SCANCODE_A]) {
		aetherTracer->entityManager->camera->moveLeft(deltaTime);
	}

	if (keys[SDL_SCANCODE_S]) {
		aetherTracer->entityManager->camera->moveBack(deltaTime);
	}

	if (keys[SDL_SCANCODE_D]) {
		aetherTracer->entityManager->camera->moveRight(deltaTime);
	}

	if (keys[SDL_SCANCODE_SPACE]) {
		aetherTracer->entityManager->camera->moveUp(deltaTime);
	}

	if (keys[SDL_SCANCODE_LCTRL]) {
		aetherTracer->entityManager->camera->moveDown(deltaTime);
	}

	if (lockMouse) {

		if (mouseX != 0 || mouseY != 0) {
			aetherTracer->entityManager->camera->updateDirection(mouseX, mouseY);
		}

	}

}

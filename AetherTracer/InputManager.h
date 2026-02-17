#pragma once

#include <iostream>
#include <SDL3/SDL.h>
#include "Vector.h"
#include "UI.h"
#include "EntityManager.h"


class InputManager {
public:

	InputManager() {};
	~InputManager() {};


	void processInput(SDL_Event& event) {
		PT::Vector2 mousePos = { event.button.x, event.button.y };

		
		if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		
			if (!UI::isWindowHovered) {
				// find clicked object in scene
			}
					
		}

		if (event.type == SDL_EVENT_KEY_DOWN) {
			
			if (event.key.scancode == SDL_SCANCODE_DELETE) {
				lockMouse = lockMouse == true ? false : true;
			}

		}
	}

	void processInputContinuous(SDL_Event& event, float deltaTime, EntityManager::Camera* camera) {
		

		SDL_GetRelativeMouseState(&mouseX, &mouseY);

		if (keys[SDL_SCANCODE_W]) {
			camera->moveForward(deltaTime);
		}

		if (keys[SDL_SCANCODE_A]) {
			camera->moveLeft(deltaTime);
		}

		if (keys[SDL_SCANCODE_S]) {
			camera->moveBack(deltaTime);
		}

		if (keys[SDL_SCANCODE_D]) {
			camera->moveRight(deltaTime);
		}
	
		if (keys[SDL_SCANCODE_SPACE]) {
			camera->moveUp(deltaTime);
		}

		if (keys[SDL_SCANCODE_LCTRL]) {
			camera->moveDown(deltaTime);
		}

		if (!lockMouse) {

			if (mouseX != 0 || mouseY != 0) {
				camera->updateDirection(mouseX, mouseY);
			}

		}
		
	}

	const bool* keys = SDL_GetKeyboardState(NULL);
	float mouseX = 0;
	float mouseY = 0;
	bool lockMouse = false;
};


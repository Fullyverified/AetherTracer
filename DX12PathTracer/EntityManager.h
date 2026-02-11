#pragma once

#include <vector>
#include <string>
#include "Vector.h"
#include "MaterialManager.h"
#include <algorithm>

class EntityManager {
public:
	struct Entity {


		Entity(std::string name) : name(name), position{ 0, 0, 0 }, rotation{ 0, 0, 0 }, material(nullptr) {};

		Entity(std::string name, PT::Vector3 position, PT::Vector3 rotation) : name(name), position(position), rotation(rotation), material(nullptr) {};

		Entity(std::string name, PT::Vector3 position, PT::Vector3 rotation, MaterialManager::Material* material) : name(name), position(position), rotation(rotation), material(material) {};

		std::string name; // name in assets folder
		PT::Vector3 position;
		PT::Vector3 rotation;
		MaterialManager::Material* material;
	};

	struct Camera {
		Camera() : position{}, rotation{} {};
		Camera(PT::Vector3 position, PT::Vector2 rotation) : position(position), rotation(rotation) {};

		PT::Vector3 position;
		PT::Vector2 rotation; // degrees

		float fovYDegrees = 60.0f;
		float aspect = 1.0f;
		float aperture = 0.0f; // DOF
		float focusDistance = 10.0f;


		PT::Vector3 forward;
		PT::Vector3 up;
		PT::Vector3 right;
		PT::Vector3 worldUp = { 0, 1, 0 };

		void update() {

			rotation.y = std::clamp(rotation.y, -89.0f, 89.0f);

			forward = PT::FromEuler(rotation);
			right = PT::Cross(forward, worldUp);
			right = PT::Normalize(right);
			up = PT::Cross(right, forward);
			up = PT::Normalize(up);

		};
	};


	EntityManager(MaterialManager* materialManager) : materialManager(materialManager) {
	
		camera = new Camera({ 0, 0, 0 }, { 0, 0 });
	
	};

	~EntityManager() {
		cleanUp();
	};

	void initScene();

	void cleanUp();


	std::vector<Entity*> entitys;
	MaterialManager* materialManager;
	Camera* camera;
};


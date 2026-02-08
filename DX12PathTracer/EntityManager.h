#pragma once

#include <vector>
#include <string>
#include "Vector.h"
#include "MaterialManager.h"

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

	EntityManager(MaterialManager* materialManager) : materialManager(materialManager) {};
	~EntityManager() {
		cleanUp();
	};

	void initScene();

	void cleanUp();


	std::vector<Entity*> entitys;
	MaterialManager* materialManager;

};


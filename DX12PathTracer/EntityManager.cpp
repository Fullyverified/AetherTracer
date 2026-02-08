#include "EntityManager.h"
#include <string>

void EntityManager::initScene() {

    //sceneObjects.emplace_back(new SceneObject{ "cube", {-2, 0, 1}, {0, 0, 0} });
    //sceneObjects.emplace_back(new SceneObject{ "weirdTriangle", {0, 0, 0}, {0, 0, 0} });
    //sceneObjects.emplace_back(new SceneObject{ "cube", {0, 2, 1}, {0, 0, 0} });

    entitys.emplace_back(new Entity{ "sphere", {-2, 0, 0}, {0, 0, 0}, materialManager->materials["Red Plastic"] });
    entitys.emplace_back(new Entity{ "weirdTriangle", {0, 0, 5}, {0, 0, 0}, materialManager->materials["Green Plastic"] });
    entitys.emplace_back(new Entity{ "companionCubeOne", {2, 0,0}, {0, 0, 0}, materialManager->materials["Blue Plastic"] });
    entitys.emplace_back(new Entity{ "sphere", {0, 1, 1}, {0, 0, 0}, materialManager->materials["White Plastic"] });



    for (Entity* entity : entitys) {
        if (entity->material == nullptr) entity->material = materialManager->materials["White Plastic"];
    }
}

void EntityManager::cleanUp() {

    for (Entity* entity : entitys) {

        auto it = std::find(entitys.begin(), entitys.end(), entity);

        entitys.erase(it);
        delete entity;
    }

}


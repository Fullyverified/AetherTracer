#include "EntityManager.h"
#include <string>


void EntityManager::initScene() {

    camera->position = { -2, 0, 0 };
    camera->rotation = { -1 , 0 };

    //sceneObjects.emplace_back(new SceneObject{ "cube", {-2, 0, 1}, {0, 0, 0} });
    //sceneObjects.emplace_back(new SceneObject{ "weirdTriangle", {0, 0, 0}, {0, 0, 0} });
    //entitys.emplace_back(new Entity{ "companionCubeOne", {5, 0, 0}, {0, 0, 0}, materialManager->materials["Blue Plastic"] });

    entitys.emplace_back(new Entity{ "cornell", {6, -6, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // floor

    entitys.emplace_back(new Entity{ "cornell", {6, 6, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // roof

    entitys.emplace_back(new Entity{ "cornell", {6, 0, 6}, {0, 0, 0}, materialManager->materials["Red Plastic"] }); // left wall
    entitys.emplace_back(new Entity{ "cornell", {6, 0, -6}, {0, 0, 0}, materialManager->materials["Green Plastic"] }); // right wall

    entitys.emplace_back(new Entity{ "cornell", {12, 0, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // back wall

    entitys.emplace_back(new Entity{ "cube", {6, 3.925, 0}, {0, 0, 0}, materialManager->materials["Light"] }); // light

    entitys.emplace_back(new Entity{ "cube", {7, -2, 1.25}, {0, -0.3, 0}, materialManager->materials["White Plastic"] });
    entitys.emplace_back(new Entity{ "cube", {7, 0, 1.25}, {0, -0.3, 0}, materialManager->materials["White Plastic"] });

    entitys.emplace_back(new Entity{ "cube", {5, -2, -1.25}, {0, 0.4, 0}, materialManager->materials["White Plastic"] });

    //entitys.emplace_back(new Entity{ "lucyScaled", {6, -3.2, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] });
    //entitys.emplace_back(new Entity{ "TheStanfordDragon", {6, -3, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] });

   
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


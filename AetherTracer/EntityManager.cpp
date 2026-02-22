#include "EntityManager.h"
#include <string>


void EntityManager::initScene() {

    camera->position = { 15, 3, 0 };
    camera->rotation = { -1 , 0 };

    // CORNELL BOX
    entitys.emplace_back(new Entity{ "cornell", {23, -3, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // floor

    entitys.emplace_back(new Entity{ "cornell", {23, 9, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // roof

    entitys.emplace_back(new Entity{ "cornell", {23, 3, 6}, {0, 0, 0}, materialManager->materials["Red Plastic"] }); // left wall
    entitys.emplace_back(new Entity{ "cornell", {23, 3, -6}, {0, 0, 0}, materialManager->materials["Green Plastic"] }); // right wall

    entitys.emplace_back(new Entity{ "cornell", {29, 3, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // back wall

    entitys.emplace_back(new Entity{ "cube", {23, 6.925, 0}, {0, 0, 0}, materialManager->materials["Light"] }); // light
    entitys.emplace_back(new Entity{ "cube", {29, 15, 0}, {0, 0, 0}, materialManager->materials["Light"] }); // high

    entitys.emplace_back(new Entity{ "cube", {24, 1, 1}, {0, -0.4, 0}, materialManager->materials["White Plastic"] });
    entitys.emplace_back(new Entity{ "cube", {24, 3, 1}, {0, -0.4, 0}, materialManager->materials["White Plastic"] });

    entitys.emplace_back(new Entity{ "cube", {22, 1, -1}, {0, 0.4, 0}, materialManager->materials["White Plastic"] });
    
    entitys.emplace_back(new Entity{ "floor", {0, 0, 0}, {0, 0, 0}, materialManager->materials["White Plastic"] }); // floor
    // CORNELL BOX

    entitys.emplace_back(new Entity{ "sphere", {15, 1, -5}, {0, 0.4, 0}, materialManager->materials["Glass"] });
    entitys.emplace_back(new Entity{ "sphere", {15, 1, -10}, {0, 0.4, 0}, materialManager->materials["Mirror"] });
    entitys.emplace_back(new Entity{ "diamondFlat", {10, 0, -7.5}, {0, 0, 0}, materialManager->materials["Diamond"] });

    entitys.emplace_back(new Entity{ "lucyScaled", {15, -0.05, 5}, {0, 0, 0}, materialManager->materials["Glass"] });
    entitys.emplace_back(new Entity{ "TheStanfordDragon", {15, 0, 10}, {0, 200, 0}, materialManager->materials["Orange Glass"] });

    entitys.emplace_back(new Entity{ "portalGun", {-5, 0, 5}, {0, 0, 0}, materialManager->materials["White Plastic"] });
    entitys.emplace_back(new Entity{ "portalButton", {-5, 0, 1}, {0, 0, 0}, materialManager->materials["White Plastic"] });
    entitys.emplace_back(new Entity{ "CompanionCube", {-5, 0, -1.5}, {0, 0, 0}, materialManager->materials["White Plastic"] });


   
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


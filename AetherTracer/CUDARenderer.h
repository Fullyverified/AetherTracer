#pragma once

#include "Renderer.h"

#include "EntityManager.h"
#include "MeshManager.h"
#include "Window.h"

class CUDARenderer : public Renderer {
public:

	CUDARenderer(EntityManager* entityManager, MeshManager* meshManager, MaterialManager* materialManager, Window* window) {};
	~CUDARenderer() {};

	void init() override;

	void resize() override;
	void render() override;
	void present() override;


	EntityManager* entityManager;
	MeshManager* meshManager;
	MaterialManager* materialManager;

	Window* window;


};
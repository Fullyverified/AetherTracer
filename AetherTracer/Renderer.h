#pragma once

class Renderer {
public:

	virtual void init() = 0;
	virtual void render() = 0;
	virtual void present() = 0;
	virtual void resize() = 0;

	virtual ~Renderer() = default;
};
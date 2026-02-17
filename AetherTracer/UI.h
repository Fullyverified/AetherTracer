#pragma once

#include <cstdint>
#include <string>

class UI {
public:

	static bool isWindowHovered;
	static void renderSettings();

	static float frameTime;
	
	static uint32_t raysPerSecond;
	static uint32_t numRays;
	static bool accumulate;

};


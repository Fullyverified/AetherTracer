#pragma once

#include <cstdint>

struct Config {

    // initial state

    uint32_t resX = 3440;
    uint32_t resY = 1440;
    uint32_t internal_resX = 0;
    uint32_t internal_resY = 0;
    float aspectX = 21;
    float aspectY = 9;

    // Multiple Importance Sampling
    int raysPerPixel = 1;
    int minBounces = 0;
    int maxBounces = 50;
    bool accumulate = true;
    bool jitter = true;

    // other
    float fOV = 45;
    bool DepthOfField = false;
    float apertureRadius = 0.05f;
    float focalDistance = 15.0f;
 
    float exposure = 1;
    bool sky = false;
    float skyBrightness = 1.0f;

    float mouseSensitivity = 0.1f;
    float sensitivity = 5.0f;

    int minBouncesMax = 100;
    int maxBouncesMax = 100;
};

extern Config config;
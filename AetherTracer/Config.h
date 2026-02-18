#pragma once

struct Config {

    // initial state

    float resX = 1200;
    float resY = 1200;
    float aspectX = 1;
    float aspectY = 1;

    // Multiple Importance Sampling
    int raysPerPixel = 1;
    int numBounces = 0;
    bool accumulate = true;

    // other
    float fOV = 45;
    bool DepthOfField = false;
    float apertureRadius = 0.05f;
    float focalDistance = 15.0f;
 
    float exposure = 1;
    bool sky = false;
    bool imgui = false;

    float mouseSensitivity = 0.1f;
    float sensitivity = 5.0f;
};

extern Config config;
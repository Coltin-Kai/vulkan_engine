#pragma once

#include "glm.hpp"
#include "vec3.hpp"

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_vulkan.h"

class Camera {
public:
	glm::vec3 pos;
	float yaw;
	float pitch;

	Camera(glm::vec3 pos = {0.0f, 0.0f, 1.0f}, float yaw = -90.0f, float pitch = 0.0f);
	void update_view_matrix();
	glm::mat4 get_view_matrix();
	void processInput(int32_t rel_mouse_x, int32_t rel_mouse_y, const uint8_t* keys);
private:
	uint64_t lastFrameTime;
	glm::mat4 view;
	glm::vec3 cameraDirection; //Manipulated by yaw and pitch.
};
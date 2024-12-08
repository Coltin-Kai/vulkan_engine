#include "Camera.h"

#include "gtc/matrix_transform.hpp"

Camera::Camera(glm::vec3 pos, float yaw, float pitch) : pos(pos), yaw(yaw), pitch(pitch) {
}

void Camera::update_view_matrix() {
	this->view = glm::lookAt(this->pos, this->pos + this->cameraDirection, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::get_view_matrix() {
	return this->view;
}

void Camera::processInput(int32_t rel_mouse_x, int32_t rel_mouse_y, const uint8_t* keys) {
	const glm::vec3 worldSpace_up = glm::vec3(0.0f, 1.0f, 0.0f);

	//May or may not move deltaTime calculations up to Engine class
	uint64_t currentFrameTime = SDL_GetTicks64();
	uint64_t deltaFrameTime = currentFrameTime - lastFrameTime;
	lastFrameTime = currentFrameTime;

	//Look Around
	const float mouseSensitivity = 0.05f;

	yaw += mouseSensitivity * static_cast<float>(rel_mouse_x);
	pitch += mouseSensitivity * static_cast<float>(-rel_mouse_y);

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	this->cameraDirection.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	this->cameraDirection.y = sin(glm::radians(pitch));
	this->cameraDirection.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
	this->cameraDirection = glm::normalize(this->cameraDirection);

	//Walk Around
	const float movementSpeed = 0.005f * static_cast<float>(deltaFrameTime);

	if (keys[SDL_SCANCODE_W])
		this->pos += movementSpeed * this->cameraDirection;
	if (keys[SDL_SCANCODE_S])
		this->pos -= movementSpeed * this->cameraDirection;
	if (keys[SDL_SCANCODE_A])
		this->pos -= movementSpeed * glm::normalize(glm::cross(this->cameraDirection, worldSpace_up));
	if (keys[SDL_SCANCODE_D])
		this->pos += movementSpeed * glm::normalize(glm::cross(this->cameraDirection, worldSpace_up));
}

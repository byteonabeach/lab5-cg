#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Camera {
public:
    glm::vec3 position  = {0.0f, 1.5f, 5.0f};
    float     yaw       = -90.0f;
    float     pitch     = 0.0f;
    float     speed     = 5.0f;
    float     mouseSens = 0.1f;
    float     fovY      = 60.0f;

    glm::vec3 front() const {
        return glm::normalize(glm::vec3{
            cos(glm::radians(yaw))  * cos(glm::radians(pitch)),
            sin(glm::radians(pitch)),
            sin(glm::radians(yaw))  * cos(glm::radians(pitch))
        });
    }
    glm::vec3 right() const {
        return glm::normalize(glm::cross(front(), glm::vec3{0,1,0}));
    }

    glm::mat4 view() const {
        return glm::lookAt(position, position + front(), glm::vec3{0,1,0});
    }

    glm::mat4 projection(float aspect) const {
        auto p  = glm::perspective(glm::radians(fovY), aspect, 0.1f, 1000.0f);
        p[1][1] *= -1; // Vulkan Y-flip
        return p;
    }

    void processKeyboard(GLFWwindow* window, float dt) {
        float v = speed * dt;
        if (glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS) position += front() * v;
        if (glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS) position -= front() * v;
        if (glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS) position -= right() * v;
        if (glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS) position += right() * v;
        if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) position.y += v;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) position.y -= v;
        speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 15.0f : 5.0f;
    }

    void processMouse(double xoffset, double yoffset) {
        yaw   += (float)xoffset * mouseSens;
        pitch -= (float)yoffset * mouseSens;
        pitch  = glm::clamp(pitch, -89.0f, 89.0f);
    }
};

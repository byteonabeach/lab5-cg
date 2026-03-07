#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <bitset>

class Input {
public:
    inline void init(GLFWwindow* win);
    inline void update();
    inline bool isKeyDown(int key) const;
    inline glm::vec2 getMouseDelta() const;
    inline bool wasResized() const;
    inline void clearResized();
    inline void setMouseCapture(bool captured);
    inline bool isMouseCaptured() const;

private:
    static inline void keyCB(GLFWwindow* window, int key, int scancode, int action, int mods);
    static inline void mouseCB(GLFWwindow* window, double xpos, double ypos);
    static inline void framebufferCB(GLFWwindow* window, int width, int height);

    GLFWwindow* window = nullptr;
    std::bitset<GLFW_KEY_LAST + 1> keys;
    glm::vec2 mousePos{0.0f};
    glm::vec2 mouseDelta{0.0f};
    bool firstMouse = true;
    bool captured = true;
    bool resized = false;
};

inline void Input::init(GLFWwindow* win) {
    window = win;
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCB);
    glfwSetCursorPosCallback(window, mouseCB);
    glfwSetFramebufferSizeCallback(window, framebufferCB);
    setMouseCapture(true);
}

inline void Input::update() {
    mouseDelta = glm::vec2(0.0f);
    glfwPollEvents();
}

inline bool Input::isKeyDown(int key) const {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        return keys.test(key);
    }
    return false;
}

inline glm::vec2 Input::getMouseDelta() const {
    return mouseDelta;
}

inline bool Input::wasResized() const {
    return resized;
}

inline void Input::clearResized() {
    resized = false;
}

inline void Input::setMouseCapture(bool c) {
    captured = c;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    firstMouse = true;
}

inline bool Input::isMouseCaptured() const {
    return captured;
}

inline void Input::keyCB(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (!input || key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) input->keys.set(key);
    else if (action == GLFW_RELEASE) input->keys.reset(key);

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        input->setMouseCapture(!input->isMouseCaptured());
    }
}

inline void Input::mouseCB(GLFWwindow* window, double xpos, double ypos) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (!input || !input->captured) return;
    glm::vec2 currentPos((float)xpos, (float)ypos);
    if (input->firstMouse) {
        input->mousePos = currentPos;
        input->firstMouse = false;
    }
    input->mouseDelta += (currentPos - input->mousePos);
    input->mousePos = currentPos;
}

inline void Input::framebufferCB(GLFWwindow* window, int width, int height) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) input->resized = true;
}

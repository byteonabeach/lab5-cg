#include "Input.h"

void Input::init(GLFWwindow* win) {
    window = win;
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCB);
    glfwSetCursorPosCallback(window, mouseCB);
    glfwSetFramebufferSizeCallback(window, framebufferCB);
    setMouseCapture(true);
}

void Input::update() {
    mouseDelta = glm::vec2(0.0f);
    glfwPollEvents();
}

bool Input::isKeyDown(int key) const {
    if (key >= 0 && key <= GLFW_KEY_LAST) {
        return keys.test(key);
    }
    return false;
}

glm::vec2 Input::getMouseDelta() const {
    return mouseDelta;
}

bool Input::wasResized() const {
    return resized;
}

void Input::clearResized() {
    resized = false;
}

void Input::setMouseCapture(bool c) {
    captured = c;
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    firstMouse = true;
}

bool Input::isMouseCaptured() const {
    return captured;
}

void Input::keyCB(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (!input || key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) input->keys.set(key);
    else if (action == GLFW_RELEASE) input->keys.reset(key);

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        input->setMouseCapture(!input->isMouseCaptured());
    }
}

void Input::mouseCB(GLFWwindow* window, double xpos, double ypos) {
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

void Input::framebufferCB(GLFWwindow* window, int width, int height) {
    Input* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) input->resized = true;
}

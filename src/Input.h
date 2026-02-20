#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>

class Input {
public:
    static Input& get() { static Input i; return i; }
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    void init(GLFWwindow* window) {
        m_window = window;
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCb);
        glfwSetCursorPosCallback(window, cursorCb);
        glfwSetMouseButtonCallback(window, mouseBtnCb);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    void beginFrame() {
        m_justPressed.clear();
        m_delta = {};
    }

    bool isDown(int key)    const { auto it = m_keys.find(key);    return it != m_keys.end()    && it->second; }
    bool pressed(int key)   const { auto it = m_justPressed.find(key); return it != m_justPressed.end() && it->second; }
    bool mouseDown(int btn) const { auto it = m_mouse.find(btn);   return it != m_mouse.end()   && it->second; }

    glm::vec2 delta() const { return m_delta; }

private:
    Input() = default;

    static void keyCb(GLFWwindow* w, int key, int, int action, int) {
        auto* s = static_cast<Input*>(glfwGetWindowUserPointer(w));
        s->m_keys[key] = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) s->m_justPressed[key] = true;
    }

    static void cursorCb(GLFWwindow* w, double x, double y) {
        auto* s = static_cast<Input*>(glfwGetWindowUserPointer(w));
        glm::vec2 pos(x, y);
        if (s->m_firstMove) { s->m_prev = pos; s->m_firstMove = false; }
        s->m_delta += pos - s->m_prev;
        s->m_prev   = pos;
    }

    static void mouseBtnCb(GLFWwindow* w, int btn, int action, int) {
        auto* s = static_cast<Input*>(glfwGetWindowUserPointer(w));
        s->m_mouse[btn] = (action == GLFW_PRESS);
    }

    GLFWwindow* m_window = nullptr;
    std::unordered_map<int, bool> m_keys, m_justPressed, m_mouse;
    glm::vec2 m_delta{}, m_prev{};
    bool m_firstMove = true;
};

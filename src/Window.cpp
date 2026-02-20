#include "Window.h"
#include <stdexcept>

std::unordered_map<GLFWwindow*, Window*> Window::s_map;

Window::Window(int w, int h, const std::string& title) : m_w(w), m_h(h) {
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }
    s_map[m_window] = this;
    glfwSetFramebufferSizeCallback(m_window, resizeCb);
}

Window::~Window() {
    s_map.erase(m_window);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

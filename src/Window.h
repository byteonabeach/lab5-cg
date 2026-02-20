#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>

class Window {
public:
    Window(int w, int h, const std::string& title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return glfwWindowShouldClose(m_window); }
    void poll()              { glfwPollEvents(); }
    void setTitle(const std::string& t) { glfwSetWindowTitle(m_window, t.c_str()); }

    GLFWwindow* handle()  const { return m_window; }
    int         width()   const { return m_w; }
    int         height()  const { return m_h; }
    float       aspect()  const { return m_h > 0 ? float(m_w) / float(m_h) : 1.f; }
    bool        resized() const { return m_resized; }
    void        clearResize()   { m_resized = false; }

private:
    static std::unordered_map<GLFWwindow*, Window*> s_map;
    static void resizeCb(GLFWwindow* w, int width, int height) {
        auto it = s_map.find(w);
        if (it == s_map.end()) return;
        it->second->m_w = width;
        it->second->m_h = height;
        it->second->m_resized = true;
    }

    GLFWwindow* m_window = nullptr;
    int  m_w, m_h;
    bool m_resized = false;
};

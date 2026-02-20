#pragma once
#include <chrono>

class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;

    Timer() { reset(); }

    void reset() {
        m_start = m_prev = Clock::now();
        m_dt = m_total = 0.f;
        m_fps = m_frames = 0;
        m_fpsAccum = 0.f;
    }

    void tick() {
        auto now   = Clock::now();
        m_dt       = std::chrono::duration<float>(now - m_prev).count();
        m_total    = std::chrono::duration<float>(now - m_start).count();
        m_prev     = now;
        if (m_dt > 0.1f) m_dt = 0.1f;
        m_fpsAccum += m_dt;
        m_frames++;
        if (m_fpsAccum >= 1.f) {
            m_fps      = m_frames;
            m_frames   = 0;
            m_fpsAccum -= 1.f;
        }
    }

    float dt()    const { return m_dt;    }
    float total() const { return m_total; }
    int   fps()   const { return m_fps;   }

private:
    Clock::time_point m_start, m_prev;
    float m_dt, m_total, m_fpsAccum;
    int   m_fps, m_frames;
};

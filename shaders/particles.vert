#version 450

struct Particle {
    vec3 pos; float life; vec3 vel; float maxLife; vec4 color;
};

layout(set = 1, binding = 0) buffer OutBuf {
    uint outVertexCount; uint outInstanceCount; uint outFirstVertex; uint outFirstInstance;
    Particle particles[];
};

layout(location = 0) out vec4 outColor;
layout(location = 1) out float outLife;
layout(location = 2) out float outMaxLife;

void main() {
    Particle p = particles[gl_VertexIndex];
    gl_Position = vec4(p.pos, 1.0);
    outColor = p.color;
    outLife = p.life;
    outMaxLife = p.maxLife;
}

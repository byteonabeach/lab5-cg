#version 450

// Lighting pass vertex shader.
// Generates a fullscreen triangle from gl_VertexIndex — no vertex buffer needed.
//
// Vertex 0: (-1, -1)   uv (0, 0)
// Vertex 1: ( 3, -1)   uv (2, 0)
// Vertex 2: (-1,  3)   uv (0, 2)
// The triangle covers the entire clip-space [-1, 1] × [-1, 1].

layout(location = 0) out vec2 outUV;

void main() {
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}

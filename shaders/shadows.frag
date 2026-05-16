#version 450

layout(location = 0) in vec2 inUV;

layout(set = 1, binding = 0) uniform sampler2D diffuseTex;

void main() {
    if (texture(diffuseTex, inUV).a < 0.5) {
        discard;
    }
}

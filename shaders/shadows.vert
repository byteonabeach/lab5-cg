#version 450
layout(location = 0) in vec3 inPosition;

layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;
layout(location = 8) in vec4 inInstanceColor;

layout(push_constant) uniform PushConstants {
    mat4 lightSpace;
} pc;

void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    gl_Position = pc.lightSpace * model * vec4(inPosition, 1.0);
}

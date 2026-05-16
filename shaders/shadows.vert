#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;
layout(location = 4) in vec4 inModel0;
layout(location = 5) in vec4 inModel1;
layout(location = 6) in vec4 inModel2;
layout(location = 7) in vec4 inModel3;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;

void main() {
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    outWorldPos = (model * vec4(inPosition, 1.0)).xyz;
    outUV = inTexCoord;
    gl_Position = vec4(outWorldPos, 1.0);
}

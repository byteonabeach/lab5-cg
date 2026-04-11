#version 450
layout(vertices = 3) out;

layout(location = 0) in vec3 inWorldPos[];
layout(location = 1) in vec3 inNormal[];
layout(location = 2) in vec2 inTexCoord[];
layout(location = 3) in vec3 inTangent[];
layout(location = 4) in vec4 inColor[];

layout(location = 0) out vec3 outWorldPos[];
layout(location = 1) out vec3 outNormal[];
layout(location = 2) out vec2 outTexCoord[];
layout(location = 3) out vec3 outTangent[];
layout(location = 4) out vec4 outColor[];

void main() {
    if (gl_InvocationID == 0) {
        gl_TessLevelInner[0] = 5.0;
        gl_TessLevelOuter[0] = 5.0;
        gl_TessLevelOuter[1] = 5.0;
        gl_TessLevelOuter[2] = 5.0;
    }
    outWorldPos[gl_InvocationID] = inWorldPos[gl_InvocationID];
    outNormal[gl_InvocationID] = inNormal[gl_InvocationID];
    outTexCoord[gl_InvocationID] = inTexCoord[gl_InvocationID];
    outTangent[gl_InvocationID] = inTangent[gl_InvocationID];
    outColor[gl_InvocationID] = inColor[gl_InvocationID];
}

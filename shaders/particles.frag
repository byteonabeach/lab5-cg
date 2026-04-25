#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 gNormal;
layout(location = 1) out vec4 gAlbedo;

void main() {
    vec2 centerDist = inTexCoord - vec2(0.5);
    if (dot(centerDist, centerDist) > 0.25) discard;

    gNormal = vec4(normalize(inNormal), 0.0);
    gAlbedo = vec4(inColor.rgb, 1.0); 
}

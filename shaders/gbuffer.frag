#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
layout(location = 4) in flat int inIsUnlit;

layout(set = 1, binding = 0) uniform sampler2D diffuseTex;

layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;

void main() {
    if (inIsUnlit != 0) {
        gPosition = vec4(inWorldPos, 1.0);
        gNormal = vec4(0.0);
        gAlbedo = inColor;
        return;
    }

    vec4 diffuse = texture(diffuseTex, inTexCoord);
    if (diffuse.a < 0.1) discard;

    gPosition = vec4(inWorldPos, 1.0);
    gNormal = vec4(normalize(inNormal), 0.0);
    gAlbedo = vec4(diffuse.rgb, 1.0);
}

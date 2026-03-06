#version 450

// Geometry pass fragment shader
// Writes per-pixel data into the three GBuffer attachments.

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// set 1: diffuse texture (provided by Engine's material descriptor)
layout(set = 1, binding = 0) uniform sampler2D diffuseTex;

// GBuffer outputs
layout(location = 0) out vec4 gPosition;  // R32G32B32A32_SFLOAT — world-space XYZ
layout(location = 1) out vec4 gNormal;    // R16G16B16A16_SFLOAT — world-space normal
layout(location = 2) out vec4 gAlbedo;    // R8G8B8A8_UNORM      — diffuse color

void main() {
    vec4 diffuse = texture(diffuseTex, inTexCoord);
    if (diffuse.a < 0.1)
        discard;

    gPosition = vec4(inWorldPos, 1.0);
    gNormal   = vec4(normalize(inNormal), 0.0);
    gAlbedo   = vec4(diffuse.rgb, 1.0);
}

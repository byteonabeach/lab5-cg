#version 450
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec4 inColor[];
layout(location = 1) in float inLife[];
layout(location = 2) in float inMaxLife[];

layout(set = 0, binding = 0) uniform GeomUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} ubo;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec4 outColor;

void main() {
    vec3 pos = gl_in[0].gl_Position.xyz;
    vec4 color = inColor[0];
    float life = inLife[0];
    float maxLife = inMaxLife[0];

    float size = 0.15 * (life / maxLife);

    vec3 right = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 up = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    
    vec3 normal = normalize(ubo.cameraPos.xyz - pos);
    vec3 tangent = right;

    vec2 uvs[4] = vec2[](vec2(0,0), vec2(1,0), vec2(0,1), vec2(1,1));
    vec2 offsets[4] = vec2[](vec2(-0.5,-0.5), vec2(0.5,-0.5), vec2(-0.5,0.5), vec2(0.5,0.5));

    for (int i = 0; i < 4; ++i) {
        vec3 worldPos = pos + right * offsets[i].x * size + up * offsets[i].y * size;
        gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
        outWorldPos = worldPos;
        outNormal = normal;
        outTexCoord = uvs[i];
        outTangent = tangent;
        outColor = color;
        EmitVertex();
    }
    EndPrimitive();
}

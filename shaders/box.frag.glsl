#version 450

layout(set = 3, binding = 0) uniform MaterialProperties {
    vec3 color;
} material;

layout(location = 0) in vec3 frag_normal;
layout(location = 1) in vec3 frag_pos;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 norm = normalize(frag_normal);
    vec3 light_dir = normalize(vec3(3, 1, 0));
    float diff = max(dot(norm, light_dir), 0.0);
    out_color = vec4((0.1 + diff) * material.color, 1.0);
}

#version 450

layout(set = 1, binding = 0) uniform VPMatrix {
    mat4 proj;
    mat4 view;
};
layout(push_constant) uniform ModelMatrix {
    mat4 model;
};

layout(location = 1) in vec3 in_pos;
layout(location = 0) in vec3 in_normal;
layout(location = 2) in vec2 in_tex_coord;

layout(location = 0) out vec3 frag_normal;
layout(location = 1) out vec2 frag_tex_coord;

void main() {
    gl_Position = proj * view * model * vec4(in_pos, 1.0);
    frag_normal = in_normal;
    frag_tex_coord = in_tex_coord;
}
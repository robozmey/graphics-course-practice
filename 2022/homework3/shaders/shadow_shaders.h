const char shadow_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 shadow_projection;

uniform mat4x3 bones[64];
uniform bool use_bones;

layout (location = 0) in vec3 in_position;
layout (location = 3) in ivec4 in_joints;
layout (location = 4) in vec4 in_weights;

void main()
{
    mat4x3 average = mat4x3(1, 0, 0,
                         0, 1, 0,
                         0, 0, 1,
                         0, 0, 0);
    if (use_bones) {
        average = bones[in_joints[0]] * in_weights[0] + bones[in_joints[1]] * in_weights[1] + bones[in_joints[2]] * in_weights[2] + bones[in_joints[3]] * in_weights[3];
        average /= 2.5;
    }

    gl_Position = shadow_projection * mat4(average) * vec4(in_position, 1.0);
}
)";

const char shadow_fragment_shader_source[] =
        R"(#version 330 core

out vec4 out_coords;

void main()
{
    float z = gl_FragCoord.z;
    float bias = 1 / 4 * (dFdx (z) * dFdx (z) + dFdy (z) * dFdy (z));
    out_coords = vec4(z, z * z + bias, 0, 0);
}
)";
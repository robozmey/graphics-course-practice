const char wolf_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform mat4x3 bones[64];

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;
layout (location = 3) in ivec4 in_joints;
layout (location = 4) in vec4 in_weights;

out vec3 normal;
out vec2 texcoord;
out vec4 weights;

void main()
{
    mat4x3 average = bones[in_joints[0]] * in_weights[0] + bones[in_joints[1]] * in_weights[1] + bones[in_joints[2]] * in_weights[2] + bones[in_joints[3]] * in_weights[3];
    gl_Position = projection * view * mat4(average) * model * vec4(in_position, 1.0);
    normal = mat3(average) * mat3(model) * in_normal;
    texcoord = in_texcoord;
    weights = in_weights;
}
)";

const char wolf_fragment_shader_source[] =
        R"(#version 330 core

uniform sampler2D albedo;
uniform vec4 color;
uniform int use_texture;
uniform vec3 camera_position;

uniform vec3 light_direction;

uniform vec3 mist_center;
uniform float mist_radius;

layout (location = 0) out vec4 out_color;

in vec3 normal;
in vec2 texcoord;
in vec4 weights;

vec2 intersect_bbox(vec3 origin, vec3 direction)
{
    origin -= mist_center;
    float a = (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    float b = 2 * (origin.x * direction.x + origin.y * direction.y + origin.z * direction.z);
    float c = (origin.x * origin.x + origin.y * origin.y + origin.z * origin.z) - mist_radius * mist_radius;

    float D = b * b - (4 * a * c);

    if (D < 0) {
        return vec2(0, 0);
    }

    float bmin = (-b + sqrt(D)) / 2;
    float bmax = (-b + sqrt(D)) / 2;

    return vec2(bmin, bmax);
}

void main()
{
    vec3 direction = -normalize(camera_position);
    vec2 tmintmax = intersect_bbox(camera_position, direction);
    float tmin = tmintmax.x;
    tmin = max(0, tmin);
    float tmax = tmintmax.y;

    float optical_depth = tmax - tmin;

    float opacity = 1.0 - exp(-optical_depth);

    vec4 albedo_color;

    if (use_texture == 1)
        albedo_color = texture(albedo, texcoord);
    else
        albedo_color = color;

    float ambient = 0.4;
    float diffuse = max(0.0, dot(normalize(normal), light_direction));

    out_color = vec4(opacity * albedo_color.rgb * (ambient + diffuse), albedo_color.a);
}
)";
const char wolf_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool use_bones;

uniform mat4x3 bones[64];

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;
layout (location = 3) in ivec4 in_joints;
layout (location = 4) in vec4 in_weights;

out vec3 position;
out vec3 normal;
out vec2 texcoord;
out vec4 weights;

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

    position = vec3(mat4(average) * model * vec4(in_position, 1.0));
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
uniform sampler2D shadow_map;
uniform mat4 shadow_projection;

uniform vec3 mist_center;
uniform float mist_radius;
uniform vec4 mist_color;

layout (location = 0) out vec4 out_color;

in vec3 position;
in vec3 normal;
in vec2 texcoord;
in vec4 weights;

vec3 intersect_bbox(vec3 origin, vec3 direction)
{
    origin -= mist_center;
    float a = (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    float b = 2 * (origin.x * direction.x + origin.y * direction.y + origin.z * direction.z);
    float c = (origin.x * origin.x + origin.y * origin.y + origin.z * origin.z) - mist_radius * mist_radius;

    if (a == 0)
        return vec3(0, 0, 0);

    float D = b * b - (4 * a * c);

    if (D < 0) {
        return vec3(0, 0, 0);
    }

    float bmin = (-b - sqrt(D)) / (2*a);
    float bmax = (-b + sqrt(D)) / (2*a);

    float cproj = -b / (2*a);

    return vec3(bmin, bmax, cproj);
}

void main()
{
    vec3 light_color = vec3(1);

    vec3 direction = -normalize(camera_position - position);
    vec3 tmintmaxcproj = intersect_bbox(camera_position, direction);
    float tmin = tmintmaxcproj.x;
    tmin = max(0, tmin);
    float tmax = tmintmaxcproj.y;
    tmax = max(0, tmax);
    float cproj = tmintmaxcproj.z;

//    if (cproj >= 0) {
//        tmin = tmax = 0;
//    }

    float optical_depth = (tmax - tmin) / mist_radius;

    float opacity = optical_depth/4;

    vec4 albedo_color;

    if (use_texture == 1)
        albedo_color = texture(albedo, texcoord);
    else
        albedo_color = color;

    vec3 ambient = vec3(0.4);
    float diffuse = max(0.0, dot(normalize(normal), light_direction));

//    opacity = 1;

    {

        vec3 albedo = albedo_color.rgb;

        vec3 color = albedo;

        vec3 final_color = vec3(mist_color) * opacity + (1 - opacity) * color * (ambient + diffuse);

        out_color = vec4(final_color, 1.0);
    }
}
)";
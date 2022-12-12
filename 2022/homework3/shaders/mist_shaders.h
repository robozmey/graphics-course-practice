static glm::vec3 mist_cube_vertices[]
        {
                {-3.f, -3.f, -3.f},
                {3.f, -3.f, -3.f},
                {-3.f, 3.f, -3.f},
                {3.f, 3.f, -3.f},
                {-3.f, -3.f, 3.f},
                {3.f, -3.f, 3.f},
                {-3.f, 3.f, 3.f},
                {3.f, 3.f, 3.f},
        };

static std::uint32_t mist_cube_indices[]
        {
                // -Z
                0, 2, 1,
                1, 2, 3,
                // +Z
                4, 5, 6,
                6, 5, 7,
                // -Y
                0, 1, 4,
                4, 1, 5,
                // +Y
                2, 6, 3,
                3, 6, 7,
                // -X
                0, 4, 2,
                2, 4, 6,
                // +X
                1, 3, 5,
                5, 3, 7,
        };

const char mist_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 view;
uniform mat4 projection;

uniform vec3 bbox_min;
uniform vec3 bbox_max;

layout (location = 0) in vec3 in_position;

out vec3 position;

void main()
{
    position = bbox_min + in_position * (bbox_max - bbox_min);
    gl_Position = projection * view * vec4(position, 1.0);
}
)";

const char mist_fragment_shader_source[] =
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
//    vec3 direction = -normalize(camera_position);
//    vec2 tmintmax = intersect_bbox(camera_position, direction);
//    float tmin = tmintmax.x;
//    tmin = max(0, tmin);
//    float tmax = tmintmax.y;
//
//    float optical_depth = tmax - tmin;
//
//    float opacity = 1.0 - exp(-optical_depth);
//
//    vec4 albedo_color = vec4(1);
//
//    opacity = 1;
//
//    out_color = vec4(albedo_color.rgb, opacity);
    out_color = vec4(1);
}
)";


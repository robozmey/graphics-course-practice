static glm::vec3 mist_cube_vertices[]
        {
                {-1.f, -1.f, -1.f},
                {1.f, -1.f, -1.f},
                {-1.f, 1.f, -1.f},
                {1.f, 1.f, -1.f},
                {-1.f, -1.f, 1.f},
                {1.f, -1.f, 1.f},
                {-1.f, 1.f, 1.f},
                {1.f, 1.f, 1.f},
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

layout (location = 0) in vec3 in_position;

out vec3 position;

void main()
{
    position = in_position*1.1;
    gl_Position = projection * view * vec4(in_position*1.1, 1.0);
}
)";

const char mist_fragment_shader_source[] =
        R"(#version 330 core

uniform sampler2D albedo;
uniform int use_texture;
uniform vec3 camera_position;

uniform vec3 light_direction;

uniform vec4 mist_color;
uniform vec3 mist_center;
uniform float mist_radius;

layout (location = 0) out vec4 out_color;

in vec3 position;

vec3 intersect_bbox(vec3 origin, vec3 direction)
{
    origin -= mist_center;
    float a = (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    float b = 2 * (origin.x * direction.x + origin.y * direction.y + origin.z * direction.z);
    float c = (origin.x * origin.x + origin.y * origin.y + origin.z * origin.z) - mist_radius * mist_radius;

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
    vec3 direction = -normalize(camera_position - position);
    vec3 tmintmaxcproj = intersect_bbox(camera_position, direction);
    float tmin = tmintmaxcproj.x;
    tmin = max(0, tmin);
    float tmax = tmintmaxcproj.y;
    tmax = max(0, tmax);

    float optical_depth = (tmax - tmin) / mist_radius;

    float opacity = optical_depth/4;

    vec4 albedo_color = mist_color;
//    albedo_color = vec4(vec3(optical_depth), 1);

    opacity = 1;

    out_color = vec4(albedo_color.rgb, opacity);
//    out_color = vec4(1);

}
)";

glm::vec4 mist_color(0.8, 0.8, 0.8, 1);


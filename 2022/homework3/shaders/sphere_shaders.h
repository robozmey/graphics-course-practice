const char sphere_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_tangent;
layout (location = 2) in vec3 in_normal;

out vec3 position;
out vec3 tangent;
out vec3 normal;

void main()
{
    position = (model * vec4(in_position, 1.0)).xyz;
    gl_Position = projection * view * vec4(position, 1.0);
    tangent = mat3(model) * in_tangent;
    normal = mat3(model) * in_normal;
}
)";

const char sphere_fragment_shader_source[] =
        R"(#version 330 core

uniform vec3 light_direction;
uniform vec3 camera_position;

uniform vec3 sphere_center;
uniform float sphere_radius;

uniform sampler2D environment_texture;

in vec3 position;
in vec3 tangent;
in vec3 normal;

layout (location = 0) out vec4 out_color;

const float PI = 3.141592653589793;

void main()
{
    float ambient_light = 0.5;

    if (distance(position, sphere_center) > sphere_radius) {
        discard;
    }

    if (position.y < 0.0) {
        discard;
    }

    vec3 bitangent = cross(tangent, normal);
    mat3 tbn = mat3(tangent, bitangent, normal);
    vec3 real_normal = tbn * (vec3(1.0));
    real_normal = normal;

    vec3 camera_direction = normalize(camera_position - position);

    vec3 dir = 2 * real_normal * dot(real_normal, camera_direction) - camera_direction;

    float x = atan(dir.z, dir.x) / PI * 0.5 + 0.5;
    float y = -atan(dir.y, length(dir.xz)) / PI + 0.5;

    float lightness = ambient_light + max(0.0, dot(normalize(real_normal), light_direction));

    vec3 albedo = vec3(ambient_light);
    vec3 environment_albedo = texture(environment_texture, vec2(x, y)).rgb;

    vec3 final_color = (lightness * albedo + environment_albedo) / 2;


    out_color = vec4(environment_albedo, 0.2);

//    out_color = vec4(lightness * albedo, 1.0);

}
)";

struct vertex
{
    glm::vec3 position;
    glm::vec3 tangent;
    glm::vec3 normal;
};

std::pair<std::vector<vertex>, std::vector<std::uint32_t>> generate_sphere(float radius, int quality, bool create_semisphere)
{
    std::vector<vertex> vertices;

    for (int latitude = -quality; latitude <= quality*(!create_semisphere); ++latitude)
    {
        for (int longitude = 0; longitude <= 4 * quality; ++longitude)
        {
            float lat = (latitude * glm::pi<float>()) / (2.f * quality);
            float lon = (longitude * glm::pi<float>()) / (2.f * quality);

            auto & vertex = vertices.emplace_back();
            vertex.normal = {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
            vertex.position = vertex.normal * radius;
            vertex.tangent = {-std::cos(lat) * std::sin(lon), 0.f, std::cos(lat) * std::cos(lon)};

        }
    }

    std::vector<std::uint32_t> indices;

    if (create_semisphere) {
        uint32_t center_index = vertices.size();

        auto &vertex = vertices.emplace_back();
        vertex.normal = {0, 1, 0};
        vertex.position = {0, 0, 0};
        vertex.tangent = {0, 0, 1};

//        uint32_t k = vertices.size();
//        for (int i = -1; i <= 1; i += 2) {
//            for (int j = -1; j <= 1; j += 2) {
//                auto &vertex = vertices.emplace_back();
//                vertex.normal = {0, 1, 0};
//                vertex.position = {i * radius, 0, j * radius};
//                vertex.tangent = {i * radius, 0, j * radius};
//            }
//        }
//        indices.insert(indices.end(), {k, k + 2, k + 1});
//        indices.insert(indices.end(), {k + 2, k + 1, k + 3});

        for (int longitude = 0; longitude < 4 * quality; ++longitude)
        {
            std::uint32_t i0 = (0 + 0) * (4 * quality + 1) + (longitude + 0);
            std::uint32_t i1 = (0 + 0) * (4 * quality + 1) + (longitude + 1);

            indices.insert(indices.end(), {i0, i1, 0});
        }
    }

    for (int latitude = 0; latitude < 2 * quality*(!create_semisphere); ++latitude)
    {
        for (int longitude = 0; longitude < 4 * quality; ++longitude)
        {
            std::uint32_t i0 = (latitude + 0) * (4 * quality + 1) + (longitude + 0);
            std::uint32_t i1 = (latitude + 1) * (4 * quality + 1) + (longitude + 0);
            std::uint32_t i2 = (latitude + 0) * (4 * quality + 1) + (longitude + 1);
            std::uint32_t i3 = (latitude + 1) * (4 * quality + 1) + (longitude + 1);

            indices.insert(indices.end(), {i0, i1, i2, i2, i1, i3});
        }
    }

    return {std::move(vertices), std::move(indices)};
}
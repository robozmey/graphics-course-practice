const char particle_vertex_shader_source[] =
        R"(#version 330 core

layout (location = 0) in vec3 in_position;
layout (location = 1) in float in_size;
layout (location = 2) in float in_rotation;

out float size;
out float rotation;

void main()
{
    gl_Position = vec4(in_position, 1.0);
    size = in_size;
    rotation = in_rotation;
}
)";

const char particle_geometry_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 camera_position;

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

in float size[];
in float rotation[];

out vec2 texcoord;

void main()
{
    vec3 center = gl_in[0].gl_Position.xyz;

    vec3 Z = -normalize(camera_position - center);

    vec3 X = normalize(vec3(Z.y, -Z.x, 0));
    vec3 Y = normalize(cross(X, Z));

    vec3 X_r = X * cos(rotation[0]) - Y * sin(rotation[0]);
    vec3 Y_r = X * sin(rotation[0]) + Y * cos(rotation[0]);

    for (int i = 0; i < 4; i++) {
        vec3 pos = center + size[0] * X_r * (i / 2 == 0 ? -1 : 1) + size[0] * Y_r * (i % 2 == 0 ? -1 : 1);
        gl_Position = projection * view * model * vec4(pos, 1.0);
        texcoord = vec2((i / 2 == 0 ? 0 : 1), (i % 2 == 0 ? 0 : 1));
        EmitVertex();
    }
    EndPrimitive();

}

)";

const char particle_fragment_shader_source[] =
        R"(#version 330 core

uniform sampler2D particle_texture;

layout (location = 0) out vec4 out_color;

in vec2 texcoord;

void main()
{
    vec4 col = texture(particle_texture, texcoord);
    out_color = col;
}
)";

const int PARTICLES_MAX_COUNT = 256;

struct particle
{
    glm::vec3 position;
    float size;
    glm::vec3 velocity;
    float rotation;
    float angular_velocity;

    particle(std::default_random_engine& rng) {
        position.x = std::uniform_real_distribution<float>{-0.3f, 0.3f}(rng);
        position.y = 0.9f;
        position.z = std::uniform_real_distribution<float>{-0.3f, 0.3f}(rng);

        size = std::uniform_real_distribution<float>{0.01f, 0.015f}(rng);

        velocity.x = std::uniform_real_distribution<float>{-0.15f, 0.15f}(rng);
        velocity.y = std::uniform_real_distribution<float>{-0.35f, -0.2}(rng);
        velocity.z = std::uniform_real_distribution<float>{-0.15f, 0.15f}(rng);

        rotation = 0;
        angular_velocity = std::uniform_real_distribution<float>{0, 0.5f}(rng);
    }
};

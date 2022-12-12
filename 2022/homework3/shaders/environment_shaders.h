const char environment_vertex_shader_source[] =
        R"(#version 330 core

const vec2 VERTICES[4] = vec2[4](
    vec2(1.0, 1.0),
    vec2(-1.0, 1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, -1.0)
);

uniform mat4 view;
uniform mat4 projection;

out vec3 position;

void main()
{
    vec2 vertex = VERTICES[gl_VertexID];
    mat4 view_projection_inverse = inverse(projection * view);
    vec4 ndc = vec4(vertex, 0.0, 1.0);
    vec4 clip_space = view_projection_inverse * ndc;
    position = clip_space.xyz / clip_space.w;
    gl_Position = vec4(vertex, 0.0, 1.0);
}
)";

const char environment_fragment_shader_source[] =
        R"(#version 330 core

uniform vec3 camera_position;
uniform float ambient_light_intensity;

uniform sampler2D environment_texture;

in vec3 position;

layout (location = 0) out vec4 out_color;

const float PI = 3.141592653589793;

void main()
{
    vec3 camera_direction = -normalize(camera_position - position);

    vec3 dir = camera_direction;
    float x = atan(dir.z, dir.x) / PI * 0.5 + 0.5;
    float y = -atan(dir.y, length(dir.xz)) / PI + 0.5;

    vec3 environment_albedo = texture(environment_texture, vec2(x, y)).rgb;

    out_color = vec4(environment_albedo * ambient_light_intensity, 1);
}
)";
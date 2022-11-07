const char vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;

out vec3 position;
out vec3 normal;
out vec2 texcoord;

void main()
{
    gl_Position = projection * view * model * vec4(in_position, 1.0);
    position = (model * vec4(in_position, 1.0)).xyz;
    normal = normalize((model * vec4(in_normal, 0.0)).xyz);
    texcoord = vec2(in_texcoord.x, 1-in_texcoord.y);
}
)";

const char fragment_shader_source[] =
        R"(#version 330 core

uniform float alpha;

uniform vec3 camera_position;

uniform vec3 ambient;

uniform vec3 glossiness;
uniform float power;

uniform vec3 light_direction;
uniform vec3 light_color;

uniform vec3 lamp_position;
uniform vec3 lamp_color;
uniform vec3 lamp_attenuation;


uniform mat4 transform;
uniform mat4 lamp_transform;

uniform sampler2D shadow_map;
uniform sampler2D texture_map;

uniform samplerCube shadow_cube_map;

in vec3 position;
in vec3 normal;
in vec2 texcoord;

layout (location = 0) out vec4 out_color;

vec3 diffuse(vec3 direction, vec3 albedo) {
    return albedo * max(0.0, dot(normal, direction));
}

vec3 specular(vec3 direction, vec3 albedo) {
    vec3 reflected_direction = 2.0 * normal * dot(normal, direction) - direction;
    vec3 view_direction = normalize(camera_position - position);
    return glossiness * albedo * pow(max(0.0, dot(reflected_direction, view_direction)), power);

}

vec3 phong(vec3 direction, vec3 albedo) {
    return diffuse(direction, albedo) + specular(direction, albedo);
}

float calc_factor(vec2 data, float sh_z) {
    float mu = data.r;
    float sigma = data.g - mu * mu;
    float z = sh_z - 0.001;
    float factor = (z < mu) ? 1.0 : sigma / (sigma + (z - mu) * (z - mu));

    float delta = 0.125;
    if (factor < delta) {
        factor = 0;
    }
    else {
        factor = (factor - delta) / (1 - delta);
    }

    return factor;
}

void main()
{
    vec4 shadow_pos = transform * vec4(position, 1.0);
    shadow_pos /= shadow_pos.w;
    shadow_pos = shadow_pos * 0.5 + vec4(0.5);

    vec2 sum = vec2(0.0);
    float sum_w = 0.0;
    const int N = 3;
    float radius = 2.0;

    for (int x = -N; x <= N; ++x) {
        for (int y = -N; y <= N; ++y) {
            float c = exp(-float(x*x + y*y) / (radius*radius));
            sum += c * texture(shadow_map, shadow_pos.xy + vec2(x,y) / vec2(textureSize(shadow_map, 0))).rg;;
            sum_w += c;
        }
    }
    vec2 data = sum / sum_w;


    float direct_factor = calc_factor(data, shadow_pos.z);

    vec3 albedo = texture(texture_map, texcoord).rgb;

    vec3 light = ambient;

    /// direct
    light += light_color * max(0.0, dot(normal, light_direction)) * direct_factor * phong(light_direction, albedo);

    /// spot

    vec4 sh_position = lamp_transform * vec4(position, 1);
    sh_position /= sh_position.w;

    float dist = sh_position.z;
    vec3 lamp_direction = normalize(lamp_position - position);

    vec4 depth = texture(shadow_cube_map, lamp_direction);
    float spot_factor = calc_factor(depth.rg, lamp_direction.z);

//    light = vec3(depth);

//    if (depth.r+0.001 >= dist) {
//        light += lamp_color;
      light += lamp_color * phong(lamp_direction, albedo);
//            * 1 / (lamp_attenuation.x + lamp_attenuation.y * dist + lamp_attenuation.z * dist * dist);
//    }


    vec3 color = albedo * light;

    if (alpha > 0.5) {
        out_color = vec4(color, 1);
    } else {
        out_color = vec4(color, 0);
    }
}
)";

const char debug_vertex_shader_source[] =
        R"(#version 330 core

vec2 vertices[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

out vec2 texcoord;

void main()
{
    vec2 position = vertices[gl_VertexID];
    gl_Position = vec4(position * 0.25 + vec2(-0.75, -0.75), 0.0, 1.0);
    texcoord = position * 0.5 + vec2(0.5);
}
)";

const char debug_fragment_shader_source[] =
        R"(#version 330 core

uniform sampler2D shadow_map;

in vec2 texcoord;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(texture(shadow_map, texcoord).rg, 0.0, 1.0);
}
)";

const char shadow_vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 projection;
uniform mat4 transform;

layout (location = 0) in vec3 in_position;

void main()
{
    gl_Position = transform * projection * model * vec4(in_position, 1.0);
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
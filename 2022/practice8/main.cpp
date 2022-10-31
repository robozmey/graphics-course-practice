#ifdef WIN32
#include <SDL.h>
#undef main
#else

#include <SDL2/SDL.h>

#endif

#include <GL/glew.h>

#include <string_view>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <cmath>
#include <fstream>
#include <sstream>

#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>

#include "obj_parser.hpp"

std::string to_string(std::string_view str)
{
    return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message)
{
    throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error)
{
    throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(glewGetErrorString(error)));
}

const char vertex_shader_source[] =
        R"(#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;

out vec3 position;
out vec3 normal;

void main()
{
    position = (model * vec4(in_position, 1.0)).xyz;
    gl_Position = projection * view * vec4(position, 1.0);
    normal = normalize(mat3(model) * in_normal);
}
)";

const char fragment_shader_source[] =
        R"(#version 330 core

uniform vec3 camera_position;

uniform vec3 albedo;

uniform vec3 sun_direction;
uniform vec3 sun_color;

uniform sampler2DShadow shadow_map;
uniform mat4 shadow_projection;


in vec3 position;
in vec3 normal;

layout (location = 0) out vec4 out_color;

vec3 diffuse(vec3 direction) {
    return albedo * max(0.0, dot(normal, direction));
}

vec3 specular(vec3 direction) {
    float power = 64.0;
    vec3 reflected_direction = 2.0 * normal * dot(normal, direction) - direction;
    vec3 view_direction = normalize(camera_position - position);
    return albedo * pow(max(0.0, dot(reflected_direction, view_direction)), power);
}

vec3 phong(vec3 direction) {
    return diffuse(direction) + specular(direction);
}

void main()
{


    float ambient_light = 0.2;

    vec3 albedo_color = albedo * ambient_light;

    vec3 phong_sum = vec3(0.0);
    float phong_sum_w = 0.0;
    const int N = 5;
    float radius = 7.0;

    for (int x = -N; x <= N; ++x) {
        for (int y = -N; y <= N; ++y) {
            float c = exp(-float(x*x + y*y) / (radius*radius));

            vec4 ndc = shadow_projection * vec4(position, 1.0);

            vec3 color = vec3(0, 0, 0);

            if (-1 <= ndc.x && ndc.x <= 1 && -1 <= ndc.y && ndc.y <= 1) {
                vec2 shadow_texcoord = ndc.xy * 0.5 + 0.5 + vec2(x,y) / vec2(textureSize(shadow_map, 0));
                float shadow_depth = ndc.z * 0.5 + 0.5;
                color = texture(shadow_map, vec3(shadow_texcoord, shadow_depth)) * sun_color * phong(sun_direction);
            } else {
                color = sun_color * phong(sun_direction);
            }

            phong_sum += c * color;
            phong_sum_w += c;
        }
    }
    vec3 final_color = albedo_color + phong_sum / phong_sum_w;

    out_color = vec4(final_color, 1.0);
}
)";

const char vertex_shader_source_rect[] =
        R"(#version 330 core

const vec2 VERTICES[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2(-0.5, -0.5),
    vec2(-1.0, -0.5),
    vec2(-0.5, -0.5),
    vec2(-1.0, -1.0),
    vec2(-0.5, -1.0)
);

out vec2 texcoord;

void main()
{
    gl_Position = vec4(VERTICES[gl_VertexID], 0.0, 1.0);
    texcoord = (VERTICES[gl_VertexID] + 0.5) * -2;
}
)";

const char fragment_shader_source_rect[] =
        R"(#version 330 core

layout (location = 0) out vec4 out_color;

uniform sampler2D sampler;

in vec2 texcoord;

void main()
{
   out_color = vec4(texture(sampler, texcoord).r);
}
)";

const char vertex_shader_source_shadow_map[] =
        R"(#version 330 core

uniform mat4 shadow_model;
uniform mat4 shadow_projection;

layout (location = 0) in vec3 in_position;

void main()
{
    gl_Position = shadow_projection * vec4(in_position, 1.0);
}
)";

const char fragment_shader_source_shadow_map[] =
        R"(#version 330 core

void main()
{
}
)";

GLuint create_shader(GLenum type, const char *source)
{
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint status;
    glGetShaderiv(result, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint info_log_length;
        glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Shader compilation failed: " + info_log);
    }
    return result;
}

GLuint create_program(GLuint vertex_shader, GLuint fragment_shader)
{
    GLuint result = glCreateProgram();
    glAttachShader(result, vertex_shader);
    glAttachShader(result, fragment_shader);
    glLinkProgram(result);

    GLint status;
    glGetProgramiv(result, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint info_log_length;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Program linkage failed: " + info_log);
    }

    return result;
}

int main()
try
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        sdl2_fail("SDL_Init: ");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow("Graphics course practice 8",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          800, 600,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");

    glClearColor(0.8f, 0.8f, 1.f, 0.f);

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    auto vertex_shader_rect = create_shader(GL_VERTEX_SHADER, vertex_shader_source_rect);
    auto fragment_shader_rect = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source_rect);
    auto program_rect = create_program(vertex_shader_rect, fragment_shader_rect);

    auto vertex_shader_shadow_map = create_shader(GL_VERTEX_SHADER, vertex_shader_source_shadow_map);
    auto fragment_shader_shadow_map = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source_shadow_map);
    auto program_shadow_map = create_program(vertex_shader_shadow_map, fragment_shader_shadow_map);

    GLuint model_location = glGetUniformLocation(program, "model");
    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint projection_location = glGetUniformLocation(program, "projection");
    GLuint camera_position_location = glGetUniformLocation(program, "camera_position");
    GLuint albedo_location = glGetUniformLocation(program, "albedo");
    GLuint sun_direction_location = glGetUniformLocation(program, "sun_direction");
    GLuint sun_color_location = glGetUniformLocation(program, "sun_color");
    GLuint shadow_projection_location_t = glGetUniformLocation(program, "shadow_projection");
    GLuint shadow_map_location = glGetUniformLocation(program, "shadow_map");

    GLuint texture_location = glGetUniformLocation(program_rect, "sampler");

    GLuint shadow_projection_location = glGetUniformLocation(program_shadow_map, "shadow_projection");
    GLuint shadow_model_location = glGetUniformLocation(program_shadow_map, "shadow_model");

    std::string project_root = PROJECT_ROOT;
    std::string scene_path = project_root + "/buddha.obj";
    obj_data scene = parse_obj(scene_path);

    GLuint scene_vao, scene_vbo, scene_ebo;
    glGenVertexArrays(1, &scene_vao);
    glBindVertexArray(scene_vao);

    glGenBuffers(1, &scene_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
    glBufferData(GL_ARRAY_BUFFER, scene.vertices.size() * sizeof(scene.vertices[0]), scene.vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &scene_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, scene.indices.size() * sizeof(scene.indices[0]), scene.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(obj_data::vertex), (void *)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(obj_data::vertex), (void *)(12));

    int shadow_map_size = 1024;

    GLuint rect_texture;
    glGenTextures(1, &rect_texture);
    glBindTexture(GL_TEXTURE_2D, rect_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);


    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,
                 shadow_map_size,
                 shadow_map_size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    GLuint rect_fbo;
    glGenFramebuffers(1, &rect_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rect_fbo);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, rect_texture, 0);

    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::runtime_error(std::to_string(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)));

    GLuint rect_vao;
    glGenVertexArrays(1, &rect_vao);


    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;

    std::map<SDL_Keycode, bool> button_down;

    float camera_distance = 1.5f;
    float camera_angle = glm::pi<float>();

    bool running = true;
    while (running)
    {
        for (SDL_Event event; SDL_PollEvent(&event);)
            switch (event.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event)
                    {
                        case SDL_WINDOWEVENT_RESIZED:
                            width = event.window.data1;
                            height = event.window.data2;
                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    button_down[event.key.keysym.sym] = true;
                    break;
                case SDL_KEYUP:
                    button_down[event.key.keysym.sym] = false;
                    break;
            }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;
        time += dt;

        if (button_down[SDLK_UP])
            camera_distance -= 4.f * dt;
        if (button_down[SDLK_DOWN])
            camera_distance += 4.f * dt;

        if (button_down[SDLK_LEFT])
            camera_angle += 2.f * dt;
        if (button_down[SDLK_RIGHT])
            camera_angle -= 2.f * dt;


        float near = 0.1f;
        float far = 100.f;

        glm::mat4 model(1.f);

        glm::mat4 view(1.f);
        view = glm::translate(view, {0.f, 0.f, -camera_distance});
        view = glm::rotate(view, glm::pi<float>() / 6.f, {1.f, 0.f, 0.f});
        view = glm::rotate(view, camera_angle, {0.f, 1.f, 0.f});
        view = glm::translate(view, {0.f, -0.5f, 0.f});

        float aspect = (float)height / (float)width;
        glm::mat4 projection = glm::perspective(glm::pi<float>() / 3.f, (width * 1.f) / height, near, far);

        glm::vec3 camera_position = (glm::inverse(view) * glm::vec4(0.f, 0.f, 0.f, 1.f)).xyz();

        glm::vec3 sun_direction = glm::normalize(glm::vec3(std::sin(time * 0.5f), 2.f, std::cos(time * 0.5f)));

        auto light_Z = -sun_direction;
        auto light_X = glm::vec3(-light_Z.y, light_Z.x + light_Z.z, -light_Z.y);
        auto norm = hypot(light_X.x, light_X.y, light_X.z);
        light_X.x /= norm;
        light_X.y /= norm;
        light_X.z /= norm;

        auto light_Y = glm::cross(light_X, light_Z);

        auto light_projection = glm::mat4(glm::transpose(glm::mat3(light_X, light_Y, light_Z)));

        ////

        glUseProgram(program_shadow_map);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rect_fbo);
        glViewport(0, 0, shadow_map_size, shadow_map_size);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glCullFace(GL_FRONT);
        glEnable(GL_CULL_FACE);
        glUniformMatrix4fv(shadow_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&light_projection));

        glBindVertexArray(scene_vao);
        glDrawElements(GL_TRIANGLES, scene.indices.size(), GL_UNSIGNED_INT, nullptr);
        glCullFace(GL_BACK);


        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.8f, 0.8f, 1.f, 0.f);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        glUseProgram(program);
        glUniformMatrix4fv(model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
        glUniformMatrix4fv(view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
        glUniformMatrix4fv(projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
        glUniform3fv(camera_position_location, 1, (float *)(&camera_position));
        glUniform3f(albedo_location, .8f, .7f, .6f);
        glUniform3f(sun_color_location, 1.f, 1.f, 1.f);
        glUniform3fv(sun_direction_location, 1, reinterpret_cast<float *>(&sun_direction));
        glUniformMatrix4fv(shadow_projection_location_t, 1, GL_FALSE, reinterpret_cast<float *>(&light_projection));

        glBindVertexArray(scene_vao);
        glDrawElements(GL_TRIANGLES, scene.indices.size(), GL_UNSIGNED_INT, nullptr);


        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glUseProgram(program_rect);
        glBindVertexArray(rect_vao);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
}
catch (std::exception const &e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

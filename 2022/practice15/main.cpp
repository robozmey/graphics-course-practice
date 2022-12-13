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
#include <fstream>
#include <chrono>
#include <vector>
#include <random>
#include <map>
#include <cmath>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "msdf_loader.hpp"
#include "stb_image.h"

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

const char msdf_vertex_shader_source[] =
R"(#version 330 core

uniform mat4 transform;

layout (location = 0) in vec2 in_position;
layout (location = 1) in vec2 in_texcoord;

out vec2 texcoord;

void main()
{
    gl_Position = transform * vec4(in_position, 0.0, 1.0);
    texcoord = in_texcoord;
}
)";

const char msdf_fragment_shader_source[] =
R"(#version 330 core

layout (location = 0) out vec4 out_color;

uniform float sdf_scale;

uniform sampler2D sdf_texture;

in vec2 texcoord;

float median(vec3 v) {
    return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

void main()
{
    vec3 font_color = vec3(0, 0, 0);
    float sdf_texture_value = median(texture(sdf_texture, texcoord).rgb);
    float sdf_value = sdf_scale * (sdf_texture_value - 0.5);
    float smopth_const = length(vec2(dFdx(sdf_value), dFdy(sdf_value)))/sqrt(2.0);
    float alpha = smoothstep(-smopth_const, smopth_const, sdf_value);

    float ob_sdf_value = sdf_scale * (sdf_texture_value - 0.3);
    float ob_smopth_const = length(vec2(dFdx(ob_sdf_value), dFdy(ob_sdf_value)))/sqrt(2.0);
    float ob_alpha = smoothstep(-ob_smopth_const, ob_smopth_const, ob_sdf_value);

    font_color = mix(vec3(1, 1, 1), font_color, alpha);

    out_color = vec4(font_color, ob_alpha);
}
)";

GLuint create_shader(GLenum type, const char * source)
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

template <typename ... Shaders>
GLuint create_program(Shaders ... shaders)
{
    GLuint result = glCreateProgram();
    (glAttachShader(result, shaders), ...);
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

struct vertex {
    glm::vec2 position;
    glm::vec2 texcoord;
};

int main() try
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

    SDL_Window * window = SDL_CreateWindow("Graphics course practice 15",
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

    auto msdf_vertex_shader = create_shader(GL_VERTEX_SHADER, msdf_vertex_shader_source);
    auto msdf_fragment_shader = create_shader(GL_FRAGMENT_SHADER, msdf_fragment_shader_source);
    auto msdf_program = create_program(msdf_vertex_shader, msdf_fragment_shader);

    GLuint transform_location = glGetUniformLocation(msdf_program, "transform");
    GLuint sdf_scale_location = glGetUniformLocation(msdf_program, "sdf_scale");

    const std::string project_root = PROJECT_ROOT;
    const std::string font_path = project_root + "/font/font-msdf.json";

    auto const font = load_msdf_font(font_path);

    GLuint texture;
    int texture_width1, texture_height1;
    {
        int channels;
        auto data = stbi_load(font.texture_path.c_str(), &texture_width1, &texture_height1, &channels, 4);
        assert(data);

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture_width1, texture_height1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);
    }

    float texture_width = texture_width1, texture_height = texture_height1;

    std::vector<vertex> vertexes;

    GLuint text_vao, text_vbo;
    glGenVertexArrays(1, &text_vao);
    glGenBuffers(1, &text_vbo);

    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)(sizeof(float) * 2));

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;

    SDL_StartTextInput();

    std::map<SDL_Keycode, bool> button_down;

    std::string text = "Do you like zucchini?";
    bool text_changed = true;

    std::vector<glm::vec2> bbox(2);

    bool running = true;
    while (running)
    {
        for (SDL_Event event; SDL_PollEvent(&event);) switch (event.type)
        {
        case SDL_QUIT:
            running = false;
            break;
        case SDL_WINDOWEVENT: switch (event.window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
                width = event.window.data1;
                height = event.window.data2;
                glViewport(0, 0, width, height);
                break;
            }
            break;
        case SDL_KEYDOWN:
            button_down[event.key.keysym.sym] = true;
            if (event.key.keysym.sym == SDLK_BACKSPACE && !text.empty())
            {
                text.pop_back();
                text_changed = true;
            }
            break;
        case SDL_TEXTINPUT:
            text.append(event.text.text);
            text_changed = true;
        case SDL_KEYUP:
            button_down[event.key.keysym.sym] = false;
            break;
        }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;

        glm::mat4 transform(0);
        transform[0][0] = 2.f / width;
        transform[1][1] = -2.f / height;
        transform[2][2] = 1;
        transform[3][3] = 1;
        transform = glm::translate(transform, {-width / 2.f, -height / 2.f, 0});

        if (text_changed) {
            glm::vec3 pen(0.0);
            vertexes.clear();
            for (auto letter : text) {
                auto glyph = font.glyphs.at(letter);
                vertexes.insert(vertexes.end(),{
                    {
                            {glyph.xoffset + pen.x, glyph.yoffset + pen.y},
                            {glyph.x / texture_width, glyph.y / texture_width}
                    }, {
                            {glyph.xoffset + glyph.width + pen.x, glyph.yoffset + pen.y},
                            {(glyph.x + glyph.width) / texture_width, glyph.y / texture_height}
                    }, {
                            {glyph.xoffset + pen.x, glyph.yoffset + glyph.height + pen.y},
                            {glyph.x / texture_width, (glyph.y + glyph.height) / texture_height}
                    }, {
                            {glyph.xoffset + glyph.width + pen.x, glyph.yoffset + pen.y},
                            {(glyph.x + glyph.width) / texture_width, glyph.y / texture_height}
                    }, {
                            {glyph.xoffset + pen.x, glyph.yoffset + glyph.height + pen.y},
                            {glyph.x / texture_width, (glyph.y + glyph.height) / texture_height}
                    }, {
                            {glyph.xoffset + glyph.width + pen.x, glyph.yoffset + glyph.height + pen.y},
                            {(glyph.x + glyph.width) / texture_width, (glyph.y + glyph.height) / texture_height}
                    }
                });
                pen.x += glyph.advance;
            }
            bbox[0].x = vertexes[0].position.x;
            bbox[0].y = vertexes[0].position.y;
            bbox[1].x = vertexes[0].position.x;
            bbox[1].y = vertexes[0].position.y;
            for (auto v : vertexes) {
                bbox[0].x = fmin(bbox[0].x, v.position.x);
                bbox[0].y = fmin(bbox[0].y, v.position.y);
                bbox[1].x = fmax(bbox[1].x, v.position.x);
                bbox[1].y = fmax(bbox[1].y, v.position.y);
            }

            glBufferData(GL_ARRAY_BUFFER,  sizeof(vertex)*vertexes.size(), vertexes.data(), GL_STATIC_DRAW);
            text_changed = false;

        }

        glm::vec2 center = glm::vec2((bbox[0].x + bbox[1].x) / 2, (bbox[0].y + bbox[1].y) / 2);
        float scale = 4;
        transform = glm::translate(transform, {width / 2.f - center.x * scale, height / 2.f - center.y * scale, 0});
        transform = glm::scale(transform, {scale, scale, 1});




        glClearColor(0.8f, 0.8f, 1.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glUseProgram(msdf_program);
        glUniform1f(sdf_scale_location, font.sdf_scale);
        glUniformMatrix4fv(transform_location, 1, GL_FALSE,
                           reinterpret_cast<float *>(&transform));

        glBindVertexArray(text_vao);
        glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
        glDrawArrays(GL_TRIANGLES, 0, vertexes.size());

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
}
catch (std::exception const & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

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
#include <random>
#include <vector>
#include <map>
#include <cmath>

#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>

#include "gltf_loader.hpp"
#include "obj_parser.hpp"
#include "stb_image.h"

#include "shaders/environment_shaders.h"
#include "shaders/sphere_shaders.h"
#include "shaders/wolf_shaders.h"
#include "shaders/particles_shaders.h"
#include "shaders/mist_shaders.h"

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


GLuint load_texture(std::string const & path)
{
    int width, height, channels;
    auto pixels = stbi_load(path.data(), &width, &height, &channels, 4);

    GLuint result;
    glGenTextures(1, &result);
    glBindTexture(GL_TEXTURE_2D, result);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(pixels);

    return result;
}

glm::mat4 rotation_matrix(glm::vec3 camera_rotation) {
    glm::mat4 view(1.f);
    view = glm::rotate(view, camera_rotation.x, {1, 0, 0});
    view = glm::rotate(view, camera_rotation.y, {0, 1, 0});
    view = glm::rotate(view, camera_rotation.z, {0, 0, 1});
    return view;
}

int main() try
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        sdl2_fail("SDL_Init: ");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window * window = SDL_CreateWindow("Graphics course homework 3",
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

    // SPHERE

    auto sphere_vertex_shader = create_shader(GL_VERTEX_SHADER, sphere_vertex_shader_source);
    auto sphere_fragment_shader = create_shader(GL_FRAGMENT_SHADER, sphere_fragment_shader_source);
    auto sphere_program = create_program(sphere_vertex_shader, sphere_fragment_shader);

    GLuint sphere_model_location = glGetUniformLocation(sphere_program, "model");
    GLuint sphere_view_location = glGetUniformLocation(sphere_program, "view");
    GLuint sphere_projection_location = glGetUniformLocation(sphere_program, "projection");
    GLuint sphere_light_direction_location = glGetUniformLocation(sphere_program, "light_direction");
    GLuint sphere_camera_position_location = glGetUniformLocation(sphere_program, "camera_position");
    GLuint sphere_albedo_texture_location = glGetUniformLocation(sphere_program, "albedo_texture");
    GLuint sphere_normal_texture_location = glGetUniformLocation(sphere_program, "normal_texture");
    GLuint sphere_environment_texture_location = glGetUniformLocation(sphere_program, "environment_texture");
    GLuint sphere_sphere_center_location = glGetUniformLocation(sphere_program, "sphere_center");
    GLuint sphere_sphere_radius_location = glGetUniformLocation(sphere_program, "sphere_radius");

    // ENVIRONMENT

    auto environment_vertex_shader = create_shader(GL_VERTEX_SHADER, environment_vertex_shader_source);
    auto environment_fragment_shader = create_shader(GL_FRAGMENT_SHADER, environment_fragment_shader_source);
    auto environment_program = create_program(environment_vertex_shader, environment_fragment_shader);

    GLuint environment_shader_view_location = glGetUniformLocation(environment_program, "view");
    GLuint environment_shader_projection_location = glGetUniformLocation(environment_program, "projection");
    GLuint environment_shader_camera_position_location = glGetUniformLocation(environment_program, "camera_position");
    GLuint environment_shader_environment_texture_location = glGetUniformLocation(environment_program, "environment_texture");

    // WOLF

    auto wolf_vertex_shader = create_shader(GL_VERTEX_SHADER, wolf_vertex_shader_source);
    auto wolf_fragment_shader = create_shader(GL_FRAGMENT_SHADER, wolf_fragment_shader_source);
    auto wolf_program = create_program(wolf_vertex_shader, wolf_fragment_shader);

    GLuint wolf_model_location = glGetUniformLocation(wolf_program, "model");
    GLuint wolf_view_location = glGetUniformLocation(wolf_program, "view");
    GLuint wolf_projection_location = glGetUniformLocation(wolf_program, "projection");
    GLuint wolf_albedo_location = glGetUniformLocation(wolf_program, "albedo");
    GLuint wolf_color_location = glGetUniformLocation(wolf_program, "color");
    GLuint wolf_use_texture_location = glGetUniformLocation(wolf_program, "use_texture");
    GLuint wolf_light_direction_location = glGetUniformLocation(wolf_program, "light_direction");
    GLuint wolf_camera_position_location = glGetUniformLocation(wolf_program, "camera_position");
    GLuint wolf_position_location = glGetUniformLocation(wolf_program, "position");
    GLuint wolf_bones_location = glGetUniformLocation(wolf_program, "bones");
    GLuint wolf_mist_radius_location = glGetUniformLocation(wolf_program, "mist_radius");
    GLuint wolf_mist_center_location = glGetUniformLocation(wolf_program, "mist_center");
    GLuint wolf_use_bones_location = glGetUniformLocation(wolf_program, "use_bones");

    // PARTICLES

    auto particle_vertex_shader = create_shader(GL_VERTEX_SHADER, particle_vertex_shader_source);
    auto particle_geometry_shader = create_shader(GL_GEOMETRY_SHADER, particle_geometry_shader_source);
    auto particle_fragment_shader = create_shader(GL_FRAGMENT_SHADER, particle_fragment_shader_source);
    auto particle_program = create_program(particle_vertex_shader, particle_geometry_shader, particle_fragment_shader);

    GLuint particle_model_location = glGetUniformLocation(particle_program, "model");
    GLuint particle_view_location = glGetUniformLocation(particle_program, "view");
    GLuint particle_projection_location = glGetUniformLocation(particle_program, "projection");
    GLuint particle_camera_position_location = glGetUniformLocation(particle_program, "camera_position");
    GLuint particle_texture_location = glGetUniformLocation(particle_program, "particle_texture");

    // MIST

    auto mist_vertex_shader = create_shader(GL_VERTEX_SHADER, mist_vertex_shader_source);
    auto mist_fragment_shader = create_shader(GL_FRAGMENT_SHADER, mist_fragment_shader_source);
    auto mist_program = create_program(mist_vertex_shader, mist_fragment_shader);

    GLuint mist_view_location = glGetUniformLocation(mist_program, "view");
    GLuint mist_projection_location = glGetUniformLocation(mist_program, "projection");
    GLuint mist_bbox_min_location = glGetUniformLocation(mist_program, "bbox_min");
    GLuint mist_bbox_max_location = glGetUniformLocation(mist_program, "bbox_max");
    GLuint mist_camera_position_location = glGetUniformLocation(mist_program, "camera_position");
    GLuint mist_light_direction_location = glGetUniformLocation(mist_program, "light_direction");
    GLuint mist_sampler_location = glGetUniformLocation(mist_program, "sampler");

    // ENVIRONMENT
    GLuint environment_vao;
    glGenVertexArrays(1, &environment_vao);
    glBindVertexArray(environment_vao);

    // LOWER SPHERE
    GLuint lower_sphere_vao, lower_sphere_vbo, lower_sphere_ebo;
    glGenVertexArrays(1, &lower_sphere_vao);
    glBindVertexArray(lower_sphere_vao);
    glGenBuffers(1, &lower_sphere_vbo);
    glGenBuffers(1, &lower_sphere_ebo);
    GLuint lower_sphere_index_count;
    {
        auto [vertices, indices] = generate_sphere(1, 16, true);

        glBindBuffer(GL_ARRAY_BUFFER, lower_sphere_vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lower_sphere_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(indices[0]), indices.data(), GL_STATIC_DRAW);

        lower_sphere_index_count = indices.size();
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, tangent));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, normal));

    // UPPER SPHERE
    GLuint upper_sphere_vao, upper_sphere_vbo, upper_sphere_ebo;
    glGenVertexArrays(1, &upper_sphere_vao);
    glBindVertexArray(upper_sphere_vao);
    glGenBuffers(1, &upper_sphere_vbo);
    glGenBuffers(1, &upper_sphere_ebo);
    GLuint upper_sphere_index_count;
    {
        auto [vertices, indices] = generate_sphere(1.f, 16, false);

        glBindBuffer(GL_ARRAY_BUFFER, upper_sphere_vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, upper_sphere_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(indices[0]), indices.data(), GL_STATIC_DRAW);

        upper_sphere_index_count = indices.size();
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, tangent));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void *)offsetof(vertex, normal));

    std::string project_root = PROJECT_ROOT;
    GLuint albedo_texture = load_texture(project_root + "/textures/brick_albedo.jpg");
    GLuint normal_texture = load_texture(project_root + "/textures/brick_normal.jpg");
    GLuint environment_texture = load_texture(project_root + "/textures/environment_map.jpg");

    // WOLF
    const std::string wolf_model_path = project_root + "/Macarena/Macarena.gltf"; //"/wolf/Wolf-Blender-2.82a.gltf";
    auto const wolf_input_model = load_gltf(wolf_model_path);
    GLuint wolf_vbo;
    glGenBuffers(1, &wolf_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, wolf_vbo);
    glBufferData(GL_ARRAY_BUFFER, wolf_input_model.buffer.size(), wolf_input_model.buffer.data(), GL_STATIC_DRAW);

    struct mesh
    {
        GLuint vao;
        gltf_model::accessor indices;
        gltf_model::material material;
    };

    auto setup_attribute = [](int index, gltf_model::accessor const & accessor, bool integer = false)
    {
        glEnableVertexAttribArray(index);
        if (integer)
            glVertexAttribIPointer(index, accessor.size, accessor.type, 0, reinterpret_cast<void *>(accessor.view.offset));
        else
            glVertexAttribPointer(index, accessor.size, accessor.type, GL_FALSE, 0, reinterpret_cast<void *>(accessor.view.offset));
    };

    std::vector<mesh> meshes;
    for (auto const & mesh : wolf_input_model.meshes)
    {
        auto & result = meshes.emplace_back();
        glGenVertexArrays(1, &result.vao);
        glBindVertexArray(result.vao);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wolf_vbo);
        result.indices = mesh.indices;

        setup_attribute(0, mesh.position);
        setup_attribute(1, mesh.normal);
        setup_attribute(2, mesh.texcoord);
        setup_attribute(3, mesh.joints, true);
        setup_attribute(4, mesh.weights);

        result.material = mesh.material;
    }

    std::map<std::string, GLuint> textures;
    for (auto const & mesh : meshes)
    {
        if (!mesh.material.texture_path) continue;
        if (textures.contains(*mesh.material.texture_path)) continue;

        auto path = std::filesystem::path(wolf_model_path).parent_path() / *mesh.material.texture_path;

        int width, height, channels;
        auto data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        assert(data);

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(data);

        textures[*mesh.material.texture_path] = texture;
    }

    // PARTICLE
    std::default_random_engine rng;

    std::vector<particle> particles;

    GLuint particle_vao, particle_vbo;
    glGenVertexArrays(1, &particle_vao);
    glBindVertexArray(particle_vao);

    glGenBuffers(1, &particle_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)(sizeof(float) * 3));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(particle), (void*)(sizeof(float) * (3 + 1 + 3)));

    GLuint particle_texture = load_texture(project_root + "/textures/snowflake.png");

    // MIST
    GLuint mist_vao, mist_vbo, mist_ebo;
    glGenVertexArrays(1, &mist_vao);
    glBindVertexArray(mist_vao);

    glGenBuffers(1, &mist_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mist_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(mist_cube_vertices), mist_cube_vertices, GL_STATIC_DRAW);

    glGenBuffers(1, &mist_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mist_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mist_cube_indices), mist_cube_indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;

    std::map<SDL_Keycode, bool> button_down;

    glm::vec3 camera_position = glm::vec3(0, -1, -2);
    glm::vec3 camera_rotation = glm::vec3(0.5, 0, 0);
    glm::vec3 camera_direction, side_direction;

    glPointSize(5.f);

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
            camera_rotation[0] -= dt;
        if (button_down[SDLK_DOWN])
            camera_rotation[0] += dt;

        if (button_down[SDLK_LEFT])
            camera_rotation[1] -= dt;
        if (button_down[SDLK_RIGHT])
            camera_rotation[1] += dt;

        camera_direction = glm::vec4(0, 0, 0.1, 1) * rotation_matrix(camera_rotation);

        if (button_down[SDLK_w])
            camera_position += camera_direction;
        if (button_down[SDLK_s])
            camera_position -= camera_direction;

        side_direction = glm::vec4(0.1, 0, 0, 1) * rotation_matrix(camera_rotation);

        if (button_down[SDLK_a])
            camera_position += side_direction;
        if (button_down[SDLK_d])
            camera_position -= side_direction;


        float A = 0, C = 0, D = 0;
        for (auto &p: particles) {
            p.velocity.y += dt * A;

            p.position += p.velocity * dt;

            p.velocity *= exp(-C * dt);

            p.size *= exp(-D * dt);

            p.rotation += p.angular_velocity * dt;

            if (p.position.y < 0 || p.position.x * p.position.x + p.position.y * p.position.y + p.position.z * p.position.z >= 1) {
                p = particle(rng);
            }
        }
        if (particles.size() < PARTICLES_MAX_COUNT) {
            particle p(rng);
            particles.push_back(p);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);

        float near = 0.1f;
        float far = 100.f;
        float top = near;
        float right = (top * width) / height;

        glm::mat4 model(1.f);

        glm::mat4 view(1.f);
        view = glm::rotate(view, camera_rotation.x, {1, 0, 0});
        view = glm::rotate(view, camera_rotation.y, {0, 1, 0});
        view = glm::rotate(view, camera_rotation.z, {0, 0, 1});
        view = glm::translate(view, camera_position);

        glm::mat4 projection = glm::mat4(1.f);
        projection = glm::perspective(glm::pi<float>() / 2.f, (1.f * width) / height, near, far);

        glm::vec3 light_direction = glm::normalize(glm::vec3(1.f, 2.f, 3.f));

        glm::vec3 camera_position = (glm::inverse(view) * glm::vec4(0.f, 0.f, 0.f, 1.f)).xyz();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, albedo_texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, normal_texture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, environment_texture);

        // ENVIRONMENT
        {
            glUseProgram(environment_program);
            glUniformMatrix4fv(environment_shader_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
            glUniformMatrix4fv(environment_shader_projection_location, 1, GL_FALSE,
                               reinterpret_cast<float *>(&projection));
            glUniform3fv(environment_shader_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
            glUniform1i(environment_shader_environment_texture_location, 2);

            glBindVertexArray(environment_vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // WOLF
        {
            glActiveTexture(GL_TEXTURE0);
            float animation_interpolation = abs(cos(time / 2));

            std::vector<glm::mat4x3> bones(wolf_input_model.bones.size(), glm::mat4x3(1));

            auto run_animation = (*wolf_input_model.animations.begin()).second;
            auto walk_animation = (*wolf_input_model.animations.begin()).second;

            auto run_frame = std::fmod(time, run_animation.max_time);
            auto walk_frame = std::fmod(time, walk_animation.max_time);

            for (int bone_index = 0; bone_index < wolf_input_model.bones.size(); bone_index++) {
                auto run_translation = run_animation.bones[bone_index].translation(run_frame);
                auto walk_translation = walk_animation.bones[bone_index].translation(walk_frame);
                auto translation = glm::translate(glm::mat4(1.f), glm::lerp((run_translation), (walk_translation),
                                                                            animation_interpolation));

                auto run_rotation = run_animation.bones[bone_index].rotation(run_frame);
                auto walk_rotation = walk_animation.bones[bone_index].rotation(walk_frame);
                auto rotation = glm::toMat4(glm::slerp(run_rotation, walk_rotation, animation_interpolation));

                auto run_scale = glm::scale(glm::mat4(1.f), run_animation.bones[bone_index].scale(run_frame));
                auto walk_scale = glm::scale(glm::mat4(1.f), walk_animation.bones[bone_index].scale(walk_frame));
                auto scale = glm::toMat4(
                        glm::slerp(glm::quat_cast(run_scale), glm::quat_cast(walk_scale), animation_interpolation));
                glm::mat4 transform = translation * rotation * scale;
                bones[bone_index] = transform;
                if (wolf_input_model.bones[bone_index].parent != -1) {
                    bones[bone_index] = bones[wolf_input_model.bones[bone_index].parent] * transform;
                }
            }
            for (int bone_index = 0; bone_index < wolf_input_model.bones.size(); bone_index++) {
                bones[bone_index] = bones[bone_index] * wolf_input_model.bones[bone_index].inverse_bind_matrix;
            }

            glUseProgram(wolf_program);
            glUniform3f(wolf_mist_center_location, 0, 0, 0);
            glUniform1f(wolf_mist_radius_location, 1);
            glUniform1f(wolf_use_bones_location, 1);
            glUniformMatrix4fv(wolf_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
            glUniformMatrix4fv(wolf_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
            glUniformMatrix4fv(wolf_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
            glUniform3fv(wolf_light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
            glUniform3fv(wolf_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
            glUniformMatrix4x3fv(wolf_bones_location, wolf_input_model.bones.size(), GL_FALSE,
                                 reinterpret_cast<float *>(bones.data()));

            auto draw_meshes = [&](bool transparent) {
                for (auto const &mesh: meshes) {
                    if (mesh.material.transparent != transparent)
                        continue;

                    if (mesh.material.two_sided)
                        glDisable(GL_CULL_FACE);
                    else
                        glEnable(GL_CULL_FACE);

                    if (transparent)
                        glEnable(GL_BLEND);
                    else
                        glDisable(GL_BLEND);

                    if (mesh.material.texture_path) {
                        glBindTexture(GL_TEXTURE_2D, textures[*mesh.material.texture_path]);
                        glUniform1i(wolf_use_texture_location, 1);
                    } else if (mesh.material.color) {
                        glUniform1i(wolf_use_texture_location, 0);
                        glUniform4fv(wolf_color_location, 1, reinterpret_cast<const float *>(&(*mesh.material.color)));
                    } else
                        continue;

                    glBindVertexArray(mesh.vao);
                    glDrawElements(GL_TRIANGLES, mesh.indices.count, mesh.indices.type,
                                   reinterpret_cast<void *>(mesh.indices.view.offset));
                }
            };
            draw_meshes(false);
            glDepthMask(GL_FALSE);
            draw_meshes(true);
            glDepthMask(GL_TRUE);
        }

        // LOWER SEMISPHERE
        {
            glm::vec4 lower_sphere_color(0.5, 0.5, 0.5, 1);

//            glEnable(GL_BLEND);
//            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glUseProgram(wolf_program);
            glUniform3f(wolf_mist_center_location, 0, 0, 0);
            glUniform1f(wolf_mist_radius_location, 1);
            glUniform1f(wolf_use_bones_location, 0);
            glUniformMatrix4fv(wolf_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
            glUniformMatrix4fv(wolf_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
            glUniformMatrix4fv(wolf_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
            glUniform3fv(wolf_light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
            glUniform3fv(wolf_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
            glUniform1i(wolf_use_texture_location, 0);
            glUniform3fv(wolf_color_location, 1, reinterpret_cast<float *>(&lower_sphere_color));

            glBindVertexArray(lower_sphere_vao);
            glDrawElements(GL_TRIANGLES, std::size(mist_cube_indices), GL_UNSIGNED_INT, nullptr);
            glCullFace(GL_BACK);
//            glDisable(GL_BLEND);
        }

        // MIST
        {
            glm::vec4 mist_color(0, 0, 0, 1);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glUseProgram(wolf_program);
            glUniform3f(wolf_mist_center_location, 0, 0, 0);
            glUniform1f(wolf_mist_radius_location, 1);
            glUniform1f(wolf_use_bones_location, 0);
            glUniformMatrix4fv(wolf_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
            glUniformMatrix4fv(wolf_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
            glUniformMatrix4fv(wolf_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
            glUniform3fv(wolf_light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
            glUniform3fv(wolf_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
            glUniform1i(wolf_use_texture_location, 0);
            glUniform3fv(wolf_color_location, 1, reinterpret_cast<float *>(&mist_color));

            glBindVertexArray(mist_vao);
            glDrawElements(GL_TRIANGLES, lower_sphere_index_count, GL_UNSIGNED_INT, nullptr);
            glCullFace(GL_BACK);
            glDisable(GL_BLEND);
        }

        // PARTICLES
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDisable(GL_CULL_FACE);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, particle_texture);

            glBindBuffer(GL_ARRAY_BUFFER, particle_vbo);
            glBufferData(GL_ARRAY_BUFFER, particles.size() * sizeof(particle), particles.data(), GL_STATIC_DRAW);

            glUseProgram(particle_program);
            glUniformMatrix4fv(particle_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
            glUniformMatrix4fv(particle_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
            glUniformMatrix4fv(particle_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
            glUniform3fv(particle_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
            glUniform1i(particle_texture_location, 2);

            glBindVertexArray(particle_vao);
            glDrawArrays(GL_POINTS, 0, particles.size());
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
        }

//        // UPPER SPHERE
//        {
//
//            glActiveTexture(GL_TEXTURE0);
//            glBindTexture(GL_TEXTURE_2D, albedo_texture);
//            glActiveTexture(GL_TEXTURE1);
//            glBindTexture(GL_TEXTURE_2D, normal_texture);
//            glActiveTexture(GL_TEXTURE2);
//            glBindTexture(GL_TEXTURE_2D, environment_texture);
//
//            glEnable(GL_BLEND);
////        glFrontFace(GL_FRONT_AND_BACK);
//            glDisable(GL_CULL_FACE);
//            glUseProgram(sphere_program);
//            glUniformMatrix4fv(sphere_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
//            glUniformMatrix4fv(sphere_view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
//            glUniformMatrix4fv(sphere_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
//            glUniform3fv(sphere_light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
//            glUniform3fv(sphere_camera_position_location, 1, reinterpret_cast<float *>(&camera_position));
//            glUniform1f(sphere_sphere_radius_location, 1);
//            glUniform1i(sphere_albedo_texture_location, 0);
//            glUniform1i(sphere_normal_texture_location, 1);
//            glUniform1i(sphere_environment_texture_location, 2);
//
//            glBindVertexArray(upper_sphere_vao);
//            glDrawElements(GL_TRIANGLES, upper_sphere_index_count, GL_UNSIGNED_INT, nullptr);
////            glFrontFace(GL_FRONT);
////        glEnable(GL_CULL_FACE);
//            glDisable(GL_BLEND);
//        }


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

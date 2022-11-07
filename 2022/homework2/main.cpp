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

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "shaders.h"

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

glm::mat4 rotation_matrix(glm::vec3 camera_rotation) {
    glm::mat4 view(1.f);
    view = glm::rotate(view, camera_rotation.x, {1, 0, 0});
    view = glm::rotate(view, camera_rotation.y, {0, 1, 0});
    view = glm::rotate(view, camera_rotation.z, {0, 0, 1});
    return view;
}

double vWidth;
double vHeight;
cv::Mat frame;
cv::Mat grayscale;

int main() try
{
    cv::VideoCapture cap("rl.mp4");
//    cv::VideoCapture cap("bad_apple_small.mp4");
    vWidth = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    vHeight = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    std::cout << vWidth << " " << vHeight << std::endl;

    if(!cap.isOpened()){
        std::cout << "Error opening video stream or file" << std::endl;
        return -1;
    }

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

    SDL_Window * window = SDL_CreateWindow("Graphics course practice 9",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

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

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    GLuint model_location = glGetUniformLocation(program, "model");
    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint projection_location = glGetUniformLocation(program, "projection");
    GLuint transform_location = glGetUniformLocation(program, "transform");
    GLuint lamp_transform_location = glGetUniformLocation(program, "lamp_transform");

    GLuint ambient_location = glGetUniformLocation(program, "ambient");
    GLuint light_direction_location = glGetUniformLocation(program, "light_direction");
    GLuint light_color_location = glGetUniformLocation(program, "light_color");

    GLuint shadow_map_location = glGetUniformLocation(program, "shadow_map");
    GLuint shadow_cube_map_location = glGetUniformLocation(program, "shadow_cube_map");
    GLuint texture_map_location = glGetUniformLocation(program, "texture_map");

    GLuint lamp_position_location = glGetUniformLocation(program, "lamp_position");
    GLuint lamp_color_location = glGetUniformLocation(program, "lamp_color");
    GLuint lamp_attenuation_location = glGetUniformLocation(program, "lamp_attenuation");

    GLuint glossiness_location = glGetUniformLocation(program, "glossiness");
    GLuint power_location = glGetUniformLocation(program, "power");

    GLuint camera_position_location = glGetUniformLocation(program, "camera_position");
    GLuint alpha_location = glGetUniformLocation(program, "alpha");

    glUseProgram(program);
    glUniform1i(shadow_map_location, 0);

    auto debug_vertex_shader = create_shader(GL_VERTEX_SHADER, debug_vertex_shader_source);
    auto debug_fragment_shader = create_shader(GL_FRAGMENT_SHADER, debug_fragment_shader_source);
    auto debug_program = create_program(debug_vertex_shader, debug_fragment_shader);

    GLuint debug_shadow_map_location = glGetUniformLocation(debug_program, "shadow_map");

    glUseProgram(debug_program);
    glUniform1i(debug_shadow_map_location, 0);

    auto shadow_vertex_shader = create_shader(GL_VERTEX_SHADER, shadow_vertex_shader_source);
    auto shadow_fragment_shader = create_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader_source);
    auto shadow_program = create_program(shadow_vertex_shader, shadow_fragment_shader);

    GLuint shadow_model_location = glGetUniformLocation(shadow_program, "model");
    GLuint shadow_transform_location = glGetUniformLocation(shadow_program, "transform");
    GLuint shadow_projection_location = glGetUniformLocation(shadow_program, "projection");


    std::string modelname = "sponza";

    std::string project_root = PROJECT_ROOT;
    std::string scene_dir = modelname;
    std::string scene_path = project_root + "/" + scene_dir + "/" + modelname + ".obj";
    std::string mtl_path = project_root + "/" + scene_dir;
    std::string textures_path = project_root + "/" + scene_dir + "/";
//    obj_data scene = parse_obj(scene_path);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, scene_path.c_str(), mtl_path.c_str());

    if (!warn.empty()) {
        std::cout << "WARN: " << warn << std::endl;
    }

    if (!err.empty()) {
        std::cerr << "ERR: " << err << std::endl;
    }

    if (!ret) {
        printf("Failed to load/parse .obj.\n");
        return false;
    }

    std::vector<GLuint> textures(materials.size());
    glGenTextures(materials.size(), textures.data());

    std::vector<bool> has_texture(materials.size(), false);

    for (int material_id = 0; material_id < materials.size(); material_id++) {

        glActiveTexture(GL_TEXTURE1);

        glBindTexture(GL_TEXTURE_2D, textures[material_id]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

        int x_size, y_size, n;
        unsigned char *data;

        if (materials[material_id].ambient_texname == "") {
            std::cout << "texture not found for material: " << material_id<< std::endl;
            continue;
        } else {

            std::string material_path = textures_path + materials[material_id].ambient_texname;

            for (auto& c : material_path)
                if (c == '\\')
                    c = '/';

            data = stbi_load(material_path.c_str(), &x_size, &y_size, &n, 4);
            std::cout << "texture: " << material_path << " " << x_size << " " << y_size << " " << n << std::endl;
        }

        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     x_size,
                     y_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);


        has_texture[material_id] = true;
    }

    GLuint vao, vbo, nbo, tbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);



    std::vector<GLuint> ebos(shapes.size());
    glGenBuffers(shapes.size(), ebos.data());

    std::vector<float> vertex_vec, normal_vec, texcoord_vec;

    int offset = 0;

    std::vector<uint> indices_all;

    for(int shape_id = 0; shape_id < shapes.size(); shape_id++) {
        int i = 0;
        std::vector<uint> indices;
        for (auto index : shapes[shape_id].mesh.indices) {
            vertex_vec.push_back(attrib.vertices[index.vertex_index * 3]);
            vertex_vec.push_back(attrib.vertices[index.vertex_index * 3 + 1]);
            vertex_vec.push_back(attrib.vertices[index.vertex_index * 3 + 2]);
            normal_vec.push_back(attrib.normals[index.normal_index * 3]);
            normal_vec.push_back(attrib.normals[index.normal_index * 3 + 1]);
            normal_vec.push_back(attrib.normals[index.normal_index * 3 + 2]);
            texcoord_vec.push_back(attrib.texcoords[index.texcoord_index * 2]);
            texcoord_vec.push_back(attrib.texcoords[index.texcoord_index * 2 + 1]);
            indices.push_back(offset + i);
            indices_all.push_back(offset + i);
            i++;
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebos[shape_id]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(indices[0]), indices.data(), GL_STATIC_DRAW);
        offset += i;
    }

    GLuint ebo_all;
    glGenBuffers(1, &ebo_all);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_all);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_all.size() * sizeof(indices_all[0]), indices_all.data(), GL_STATIC_DRAW);

    int vertex_count = offset;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(attrib.vertices[0]) * 3, vertex_vec.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &nbo);
    glBindBuffer(GL_ARRAY_BUFFER, nbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(attrib.normals[0]) * 3, normal_vec.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &tbo);
    glBindBuffer(GL_ARRAY_BUFFER, tbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(attrib.texcoords[0]) * 2, texcoord_vec.data(), GL_STATIC_DRAW);



    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(attrib.vertices[0]) * 3, (void*)(0));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, nbo);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(attrib.normals[0]) * 3, (void*)(0));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, tbo);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(attrib.texcoords[0]) * 2, (void*)(0));

    GLuint debug_vao;
    glGenVertexArrays(1, &debug_vao);

    GLsizei shadow_map_resolution = 1024;

    GLuint shadow_map;
    glGenTextures(1, &shadow_map);
    glBindTexture(GL_TEXTURE_2D, shadow_map);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, shadow_map_resolution, shadow_map_resolution, 0, GL_RGBA, GL_FLOAT, nullptr);

    GLuint shadow_fbo;
    glGenFramebuffers(1, &shadow_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadow_map, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Incomplete framebuffer!");

    GLuint shadow_rbo;
    glGenRenderbuffers(1, &shadow_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, shadow_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, shadow_map_resolution, shadow_map_resolution);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, shadow_rbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    float bounding_box[3][2] = {{0, 0}, {0, 0}, {0, 0}};

    for (size_t vertex_id = 0; vertex_id < attrib.vertices.size(); vertex_id += 3) {
        for (int i = 0; i < 3; i++) {
            bounding_box[i][0] = std::min(bounding_box[i][0], attrib.vertices[vertex_id + i]);
            bounding_box[i][1] = std::max(bounding_box[i][1], attrib.vertices[vertex_id + i]);
        }
    }

    glm::vec3 bounding0 = glm::vec3(bounding_box[0][0], bounding_box[1][0], bounding_box[2][0]);
    glm::vec3 bounding1 = glm::vec3(bounding_box[0][1], bounding_box[1][1], bounding_box[2][1]);

    glm::vec3 C = (bounding0 + bounding1);


    std::cout << glm::to_string(bounding0) << " " << glm::to_string(bounding1) << std::endl;


    glActiveTexture(GL_TEXTURE0);
    GLuint shadow_cube_map;
    glGenTextures(1, &shadow_cube_map);
    glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_cube_map);
    for (int side_id = 0; side_id < 6; side_id++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + side_id, 0, GL_RG32F, shadow_map_resolution, shadow_map_resolution, 0,
                     GL_RGBA, GL_FLOAT, nullptr);
    }
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint shadow_cube_fbo[6];
    glGenFramebuffers(6, shadow_cube_fbo);

    GLuint shadow_cube_rbo[6];
    glGenRenderbuffers(6, shadow_cube_rbo);

    for (int side_id = 0; side_id < 6; side_id++) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_cube_fbo[side_id]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side_id,
                               shadow_cube_map, 0);
        std::cout << glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) << std::endl;
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete framebuffer!");

        glBindRenderbuffer(GL_RENDERBUFFER, shadow_cube_rbo[side_id]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, shadow_map_resolution, shadow_map_resolution);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, shadow_cube_rbo[side_id]);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }



    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;
    bool paused = false;

    std::map<SDL_Keycode, bool> button_down;


    bool running = true;

    bool trasparent = false;

    glm::vec3 camera_position = glm::vec3(0, -200, 0);
    glm::vec3 camera_rotation = glm::vec3(0, -1.4, 0);
    glm::vec3 camera_direction, side_direction;

    GLuint rl_texture;
    glGenTextures(1, &rl_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

    while (running)
    {

        usleep(1e6 / 25);
        bool updated = false;
        cap >> frame;
        if(frame.empty())
            cap = cv::VideoCapture("rl.mp4");
        cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, rl_texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

        int x_size, y_size, n;
        unsigned char *rl_data = frame.data;

        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB8,
                     vWidth,
                     vHeight, 0, GL_BGR, GL_UNSIGNED_BYTE, rl_data);
        glGenerateMipmap(GL_TEXTURE_2D);


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

            if (event.key.keysym.sym == SDLK_SPACE)
                paused = !paused;

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
        if (!paused)
            time += dt;


        if (button_down[SDLK_UP])
            camera_rotation[0] -= dt;
        if (button_down[SDLK_DOWN])
            camera_rotation[0] += dt;

        if (button_down[SDLK_LEFT])
            camera_rotation[1] -= dt;
        if (button_down[SDLK_RIGHT])
            camera_rotation[1] += dt;

        camera_direction = glm::vec4(0, 0, 10, 1) * rotation_matrix(camera_rotation);

        if (button_down[SDLK_w])
            camera_position += camera_direction;
        if (button_down[SDLK_s])
            camera_position -= camera_direction;

        side_direction = glm::vec4(10, 0, 0, 1) * rotation_matrix(camera_rotation);

        if (button_down[SDLK_a])
            camera_position += side_direction;
        if (button_down[SDLK_d])
            camera_position -= side_direction;

        if (button_down[SDLK_t])
            trasparent = !trasparent;

        ///
        glViewport(0, 0, width, height);

        glm::mat4 model(1.f);

        glm::vec3 light_direction = glm::normalize(glm::vec3(std::cos(time * 0.125f), 1.f, std::sin(time * 0.125f)));

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, shadow_map_resolution, shadow_map_resolution);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glm::vec3 light_z = glm::normalize(-light_direction);
        glm::vec3 light_x = glm::normalize(glm::cross(light_z, {0.f, 1.f, 0.f}));
        glm::vec3 light_y = glm::normalize(glm::cross(light_x, light_z));
        float shadow_scale = 2.f;
        float shadow_scale_x = -1e8;
        float shadow_scale_y = -1e8;
        float shadow_scale_z = -1e8;

        for (int i = 0; i < 8; i++) {
            glm::vec3 V = glm::vec3(bounding_box[0][i / 4], bounding_box[1][i / 2 % 2], bounding_box[2][i % 2]);

            float tmp;
            tmp = glm::dot(V - C, light_x);
            shadow_scale_x = std::max(shadow_scale_x, std::abs(tmp));

            tmp = glm::dot(V - C, light_y);
            shadow_scale_y = std::max(shadow_scale_y, std::abs(tmp));

            tmp = glm::dot(V - C, light_z);
            shadow_scale_z = std::max(shadow_scale_z, std::abs(tmp));
        }

//        std::cout << shadow_scale_x << std::endl;

        glm::mat4 transform = glm::mat4(1.f);
        for (size_t i = 0; i < 3; ++i)
        {
            transform[i][0] = shadow_scale_x * light_x[i];
            transform[i][1] = shadow_scale_y * light_y[i];
            transform[i][2] = shadow_scale_z * light_z[i];
            transform[i][3] = C[i];
        }

        transform = glm::transpose(transform);
        transform = glm::inverse(transform);

        glUseProgram(shadow_program);
        glUniformMatrix4fv(shadow_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
        glUniformMatrix4fv(shadow_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
        glUniformMatrix4fv(shadow_transform_location, 1, GL_FALSE, reinterpret_cast<float *>(&transform));

        glBindVertexArray(vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_all);
        glDrawElements(GL_TRIANGLES, indices_all.size(), GL_UNSIGNED_INT, nullptr);

        glBindTexture(GL_TEXTURE_2D, shadow_map);
        glGenerateMipmap(GL_TEXTURE_2D);

        ///
        glm::vec3 lamp_position = glm::vec3(sin(time) * 800, 200, -500);

        glm::mat4 lamp_transform;
        glm::mat4 shadowProj;
        {

            float aspect = (float) shadow_map_resolution / (float) shadow_map_resolution;
            float near = 1.0f;
            float far = 25.0f;
            shadowProj = glm::perspective(glm::radians(90.0f), aspect, near, far);

            for (int side_id = 0; side_id < 6; side_id++) {
                glViewport(0, 0, width, height);

                glActiveTexture(GL_TEXTURE2);

                glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_cube_map);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_cube_fbo[side_id]);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glViewport(0, 0, shadow_map_resolution, shadow_map_resolution);

//                glm::vec3 light_z(0, 0, 1);
//                glm::vec3 light_x = glm::normalize(glm::cross(light_z, {0.f, 1.f, 0.f}));
//                glm::vec3 light_y = glm::normalize(glm::cross(light_x, light_z));


                lamp_transform = glm::mat4(1.f);
                glm::vec3 rot = (light_x * float(side_id / 2 == 0) * float(side_id % 2 * 2 - 1) +
                                light_y * float(side_id / 2 == 1) * float(side_id % 2 * 2 - 1) +
                                light_z * float(side_id / 2 == 2) * float(side_id % 2 * 2 - 1));
                lamp_transform = glm::rotate(lamp_transform, float(std::numbers::pi / 2), rot);
//
//                transform = glm::rotate(transform, rot.x, {1, 0, 0});
//                transform = glm::rotate(transform, rot.y, {0, 1, 0});
//                transform = glm::rotate(transform, rot.z, {0, 0, 1});


                lamp_transform = glm::translate(lamp_transform, lamp_position);
//                transform = glm::inverse(transform);


                glUseProgram(shadow_program);
                glUniformMatrix4fv(shadow_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
                glUniformMatrix4fv(shadow_projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&shadowProj));
                glUniformMatrix4fv(shadow_transform_location, 1, GL_FALSE, reinterpret_cast<float *>(&lamp_transform));

                glBindVertexArray(vao);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_all);
                glDrawElements(GL_TRIANGLES, indices_all.size(), GL_UNSIGNED_INT, nullptr);

//                glViewport(0, 0, width, height);

            }
//            glBindTexture(GL_TEXTURE_CUBE_MAP, shadow_cube_map);
//            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

//            trasparent = true;

        }
        float *data = new float[shadow_map_resolution * shadow_map_resolution * 4];
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, GL_FLOAT, data);
        std::cout << "lol: ";
        for (int i = 0; i< 20; i++) {
            std::cout << data[i] << " ";
        }
        std::cout << std::endl;
        delete[] data;
        ///

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);

        glClearColor(0.8f, 0.8f, 0.9f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        float near = 10.f;
        float far = 4000.f;

        glm::mat4 view(1.f);
        view = glm::rotate(view, camera_rotation.x, {1, 0, 0});
        view = glm::rotate(view, camera_rotation.y, {0, 1, 0});
        view = glm::rotate(view, camera_rotation.z, {0, 0, 1});
        view = glm::translate(view, camera_position);


        glm::mat4 projection = glm::mat4(1.f);
        projection = glm::perspective(glm::pi<float>() / 2.f, (1.f * width) / height, near, far);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shadow_map);


        glUseProgram(program);
        glUniformMatrix4fv(model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
        glUniformMatrix4fv(view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
        glUniformMatrix4fv(projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection));
        glUniformMatrix4fv(transform_location, 1, GL_FALSE, reinterpret_cast<float *>(&transform));
        glUniformMatrix4fv(lamp_transform_location, 1, GL_FALSE, reinterpret_cast<float *>(&lamp_transform));
/    if (depth.r+0.001 >= dist) {
//        light += lamp_color;
      light += lamp_color * phong(lamp_direction, albedo);
//            * 1 / (lamp_attenuation.x + lamp_attenuation.y * dist + lamp_attenuation.z * dist * dist);
//    }
        glUniform3fv(camera_position_location, 1, reinterpret_cast<float *>(&camera_position));

        glUniform3f(ambient_location, 0.4f, 0.4f, 0.4f);

        glUniform3fv(light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
        glUniform3f(light_color_location, 0.8f, 0.8f, 0.8f);

        glUniform3fv(lamp_position_location, 1, reinterpret_cast<float *>(&lamp_position));
        glUniform3f(lamp_color_location, 0.8f, 0.8f, 0.0f);
        glUniform3f(lamp_attenuation_location, 1.0, 0.0, 1);

        glUniform1i(shadow_cube_map, 1);


        glBindVertexArray(vao);

        for(int shape_id = 0; shape_id < shapes.size(); shape_id++) {

            int material_id = shapes[shape_id].mesh.material_ids[0];

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, textures[material_id]);

            glUniform1f(alpha_location, materials[material_id].dissolve);

            glUniform3fv(glossiness_location, 1, materials[material_id].specular);
            glUniform1f(power_location, materials[material_id].shininess);

            if (materials[material_id].dissolve < 0.95) {
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }

//            std::cout << materials[material_id].dissolve << std::endl;


            if (!has_texture[material_id])
                glBindTexture(GL_TEXTURE_2D, rl_texture);

            glUniform1i(texture_map_location, 1);

            glUniform1i(shadow_cube_map_location, 2);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebos[shape_id]);
            glDrawElements(GL_TRIANGLES, shapes[shape_id].mesh.indices.size(), GL_UNSIGNED_INT, nullptr);

            glDisable(GL_BLEND);
        }

        glUseProgram(debug_program);
        glBindTexture(GL_TEXTURE_2D, shadow_map);
        glBindVertexArray(debug_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

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

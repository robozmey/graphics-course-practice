#ifdef WIN32
#include <SDL.h>
#undef main
#else
#include <SDL2/SDL.h>
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <GL/glew.h>

#include <string_view>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <atomic>

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

uniform mat4 view;
uniform float time;

layout (location = 0) in vec2 in_position;
layout (location = 1) in vec4 in_color;

out vec4 color;

float f(float x, float y, float t) {
    x *= 5;
    y *= 5;
    return abs(sin(x + t*2 + y) + cos(y) + cos(x) * sin(t) + sin(t) + sin(y) + sin(y  * cos(t) * x)) / 6;
}

void main()
{
    gl_Position = view * vec4(in_position, 0.0, 1.0);
    float brightness = f(in_position.x, in_position.y, time);
  //  color = vec4(brightness, 1 - brightness, 0, 1.0);
    color = in_color;
}
)";

const char fragment_shader_source[] =
R"(#version 330 core

in vec4 color;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = color;
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

struct vec2
{
    float x;
    float y;
};

struct color_t
{
    std::uint8_t color[4];
    color_t() {
        color[0] = color[1] = color[2] = 0;
        color[3] = 255;
    }
};

double vWidth;
double vHeight;
cv::Mat frame;
cv::Mat grayscale;
float f2(float x, float y, float t) {
    x *= 5;
    y *= 5;
    return (abs(sin(x + t*2 + y) + cos(y) + cos(x) * sin(t) + sin(t) + sin(y) + sin(y  * cos(t) * x)) / 6);
}

long res_x;
long res_y;

float f(float x, float y, float t) {
    return grayscale.at<cv::Vec3b>(int((0.5-y) * (vHeight-1)), int((x/4 - 0.25) * (vHeight-1)))[0] / 255.f;
}

void init_points(std::vector<vec2>& points_poses, std::vector<color_t>& points_colors) {
    points_poses.resize(res_x * res_y);
    points_colors.resize(res_x * res_y);
#pragma omp parallel for
    for (int i = 0; i < res_x; i++) {
#pragma omp parallel
        for (int j = 0; j < res_y; j++) {
            points_poses[i*res_y + j] = {float(i - float(res_x-1)/2) / float(res_y-1), float(j - float(res_y-1)/2) / float(res_y-1)};
            points_colors[i*res_y + j] = color_t();
        }
    }
}

void fill_indices(std::vector<unsigned int>& indices) {

    for (int i = 0; i < res_x-1; i++) {
        for (int j = 0; j < res_y-1; j++) {
            indices.push_back(i * res_y + j);
            indices.push_back((i + 1) * res_y + j);
            indices.push_back(i * res_y + (j + 1));
            indices.push_back((i + 1) * res_y + (j + 1));
            indices.push_back((i + 1) * res_y + j);
            indices.push_back(i * res_y + (j + 1));
        }
    }
}

void paint_points(const std::vector<vec2>& points_poses, std::vector<color_t>& points_colors, float time) {
    for (int i = 0; i < points_colors.size(); i++) {
        auto& pc = points_colors[i];
        auto& pp = points_poses[i];
        auto res = f(pp.x, pp.y, time) * 255;
        pc.color[0] = res;
        pc.color[1] = 255 - res;
        pc.color[2] = 255 - res;
    }
}

void create_isolines(const std::vector<vec2>& points_poses,
                     const std::vector<color_t>& points_colors,
                     const std::vector<float>& iso_borders,
                     std::vector<unsigned int>& isoindices,
                     std::vector<vec2>& isopoints) {

    std::map<std::pair<int, std::pair<int, int>>, int> isopoints_indices;
    for (int iso_border_index = 0; iso_border_index < iso_borders.size(); iso_border_index++) {
        int border = 255.f * iso_borders[iso_border_index];

        for (int i = 0; i < res_x - 1; i++) {
            for (int j = 0; j < res_y - 1; j++) {
                /// a b
                /// c d
                auto& a = points_colors[i * res_y + j].color[0];
                auto& b = points_colors[(i + 1) * res_y + j].color[0];
                auto& c = points_colors[i * res_y + (j + 1)].color[0];
                auto& d = points_colors[(i + 1) * res_y + (j + 1)].color[0];

                auto center = (points_colors[i * res_y + j].color[0]
                               + points_colors[(i + 1) * res_y + j].color[0]
                               + points_colors[i * res_y + (j + 1)].color[0]
                               + points_colors[(i + 1) * res_y + (j + 1)].color[0]) / 4;

                auto ba = a > border;
                auto bb = b > border;
                auto bc = c > border;
                auto bd = d > border;

                auto bcenter = center > border;

                /// + +
                /// + +
                if (ba == bb && bb == bc && bc == bd) {

                } else {

                    auto &x = points_poses[i * res_y + j].x;
                    auto &y = points_poses[i * res_y + j].y;
                    auto &x2 = points_poses[(i + 1) * res_y + (j + 1)].x;
                    auto &y2 = points_poses[(i + 1) * res_y + (j + 1)].y;

                    /// a b
                    /// c d

                    ///   v1
                    /// v2  v4
                    ///   v3
                    auto v1x = ((x - x2) * border + a * x2 - b * x) / (a - b);
                    auto v2y = ((y - y2) * border + a * y2 - c * y) / (a - c);
                    auto v3x = ((x - x2) * border + c * x2 - d * x) / (c - d);
                    auto v4y = ((y - y2) * border + b * y2 - d * y) / (b - d);

                    /// v1
                    std::pair<int, std::pair<int, int>> v1p = {iso_border_index * 2, {i, j}};
                    if (!isopoints_indices.contains(v1p)) {
                        isopoints_indices[v1p] = isopoints.size();
                        isopoints.push_back({v1x, y});
                    }
                    auto v1id = isopoints_indices[v1p];

                    /// v2
                    std::pair<int, std::pair<int, int>> v2p = {iso_border_index * 2 + 1, {i, j}};
                    if (!isopoints_indices.contains(v2p)) {
                        isopoints_indices[v2p] = isopoints.size();
                        isopoints.push_back({x, v2y});
                    }
                    auto v2id = isopoints_indices[v2p];

                    /// v3
                    std::pair<int, std::pair<int, int>> v3p = {iso_border_index * 2, {i, j + 1}};
                    if (!isopoints_indices.contains(v3p)) {
                        isopoints_indices[v3p] = isopoints.size();
                        isopoints.push_back({v3x, y2});
                    }
                    auto v3id = isopoints_indices[v3p];

                    /// v4
                    std::pair<int, std::pair<int, int>> v4p = {iso_border_index * 2 + 1, {i + 1, j}};
                    if (!isopoints_indices.contains(v4p)) {
                        isopoints_indices[v4p] = isopoints.size();
                        isopoints.push_back({x2, v4y});
                    }
                    auto v4id = isopoints_indices[v4p];

                    /// + -
                    /// - -
                    if (ba != bb && bb == bc && bc == bd) {
                        isoindices.push_back(v1id);
                        isoindices.push_back(v2id);
                    } else if (bb != bc && bc == bd && bd == ba) {
                        isoindices.push_back(v1id);
                        isoindices.push_back(v4id);
                    } else if (bc != bd && bd == ba && ba == bb) {
                        isoindices.push_back(v3id);
                        isoindices.push_back(v2id);
                    } else if (bd != ba && ba == bb && bb == bc) {
                        isoindices.push_back(v3id);
                        isoindices.push_back(v4id);
                    }
                    /// + +
                    /// - -
                    else if (ba == bb && bb != bc && bc == bd) {
                        isoindices.push_back(v2id);
                        isoindices.push_back(v4id);
                    } else if (ba == bc && ba != bb && bb == bd) {
                        isoindices.push_back(v1id);
                        isoindices.push_back(v3id);
                    }
                    /// + -
                    /// - +
                    else if (bcenter == ba && ba != bb && bb != bd && bd != bc) {
                        isoindices.push_back(v1id);
                        isoindices.push_back(v2id);
                        isoindices.push_back(v3id);
                        isoindices.push_back(v4id);
                    } else if (bcenter != ba && ba != bb && bb != bd && bd != bc) {
                        isoindices.push_back(v1id);
                        isoindices.push_back(v2id);
                        isoindices.push_back(v3id);
                        isoindices.push_back(v4id);
                    } else if (ba == bb && bb == bc && bc == bd) {

                    } else {
                        std::cout << ba << " " << bb << " " << bc << " " << bd << std::endl;
                    }
                }

            }
        }
    }
}

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
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window * window = SDL_CreateWindow("Graphics course homework 1",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    SDL_GL_SetSwapInterval(0);

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");

    glClearColor(0.8f, 0.8f, 1.f, 0.f);

    auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    auto program = create_program(vertex_shader, fragment_shader);

    GLuint view_location = glGetUniformLocation(program, "view");
    GLuint time_location = glGetUniformLocation(program, "time");

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    int W = 4;
    int H = 3;

    int quality = 20;
    int partition = 2;

    res_x = W * quality + 1;
    res_y = H * quality + 1;

    std::vector<vec2> points_poses;
    std::vector<color_t> points_colors;

    init_points(points_poses, points_colors);

    std::vector<unsigned int> indices;
    fill_indices(indices);

    GLuint poses_vbo;
    glGenBuffers(1, &poses_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, poses_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vec2) * points_poses.size(), points_poses.data(), GL_STATIC_DRAW);

    GLuint colors_vbo;
    glGenBuffers(1, &colors_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_t) * points_colors.size(), points_colors.data(), GL_DYNAMIC_DRAW);



    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_DYNAMIC_DRAW);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, poses_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(vec2), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(color_t), (void*)0);



    GLuint iso_vbo;
    glGenBuffers(1, &iso_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, iso_vbo);

    GLuint iso_vao;
    glGenVertexArrays(1, &iso_vao);
    glBindVertexArray(iso_vao);
    glBindBuffer(GL_ARRAY_BUFFER, iso_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(vec2), (void*)0);

    GLuint iso_ebo;
    glGenBuffers(1, &iso_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iso_ebo);

    float time = 0.f;

    bool running = true;
    while (running)
    {
        usleep(1e6 / 25);
        bool updated = false;
        cap >> frame;
        cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);

//         If the frame is empty, break immediately
        if (frame.empty())
            break;

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
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                float mouse_x = event.button.x;
                float mouse_y = event.button.y;

                updated = true;
            }
            else if (event.button.button == SDL_BUTTON_RIGHT)
            {

                updated = true;
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_LEFT)
            {
                if (quality > 1) {
                    quality -= 1;
                    updated = true;
                }
            }
            else if (event.key.keysym.sym == SDLK_RIGHT)
            {
                if (true) { //W * (quality+1) + 1 < vWidth && H * (quality+1) + 1 < vHeight
                    quality += 1;
                    updated = true;
                } else {
                    std::cout << "Too large quality" << std::endl;
                }
            }
            if (event.key.keysym.sym == SDLK_DOWN)
            {
                if (partition > 1) {
                    partition -= 1;
                    updated = true;
                }
            }
            else if (event.key.keysym.sym == SDLK_UP)
            {
                partition += 1;
            }
            break;
        }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;
        time += dt;

        float aspect_ratio = float(width) / height;

        if (updated) {
            res_x = W * quality + 1;
            res_y = H * quality + 1;

            init_points(points_poses, points_colors);
            fill_indices(indices);

            glBindBuffer(GL_ARRAY_BUFFER, poses_vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vec2) * points_poses.size(), points_poses.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * indices.size(), indices.data(), GL_DYNAMIC_DRAW);
        }

        paint_points(points_poses, points_colors, time);

        glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(color_t) * points_colors.size(), points_colors.data(), GL_DYNAMIC_DRAW);

        std::vector<float> iso_borders;
        for (int i = 0; i < partition-1; i++) {
            iso_borders.push_back(1.0 / partition * (i+1));
        }

        std::vector<vec2> isopoints;
        std::vector<unsigned int> isoindices;

        create_isolines(points_poses, points_colors, iso_borders, isoindices, isopoints);


        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iso_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * isoindices.size(), isoindices.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, iso_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec2) * isopoints.size(), isopoints.data(), GL_DYNAMIC_DRAW);

        glClear(GL_COLOR_BUFFER_BIT);

        float view[16] =
        {
                1.f/aspect_ratio, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f,
        };

        glUseProgram(program);
        glUniformMatrix4fv(view_location, 1, GL_TRUE, view);
        glUniform1f(time_location, time);

//        glBindVertexArray(vao);
//        glBindBuffer(GL_ARRAY_BUFFER, vbo);
//        glDrawArrays(GL_TRIANGLES, 0, points.size());
//        glPointSize(10);
//        glDrawArrays(GL_POINTS, 0, points.size());

        glBindVertexArray(vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, (void*)0);

        glBindVertexArray(iso_vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iso_ebo);
        glBindBuffer(GL_ARRAY_BUFFER, iso_vbo);
//        glLineWidth(5.f);
        glDrawElements(GL_LINES, isoindices.size(), GL_UNSIGNED_INT, (void*)0);

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);

//    cap.release();
}
catch (std::exception const & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

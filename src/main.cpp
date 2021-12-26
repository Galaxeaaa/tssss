#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#include "camera.hpp"
#include "model.hpp"
#include "shader.hpp"

struct Light {
  glm::vec4 position;
  glm::vec4 color;
  float radius;
  float dummy[3];  // for alignment with std430
};

struct DebugBuffer {
  int nlight;
  float mindepth;
  float maxdepth;
};

#define TILE_SIZE 16
#define MAX_NLIGHTS_PER_TILE 2048
#define N_LIGHTS 2048

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);
unsigned int loadTexture(const char *path, bool gammaCorrection);
void renderQuad();
void renderCube();

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 5.0f));
float lastX = (float)SCR_WIDTH / 2.0;
float lastY = (float)SCR_HEIGHT / 2.0;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

int nWorkGroupsX = (SCR_WIDTH + (SCR_WIDTH % TILE_SIZE)) / TILE_SIZE;
int nWorkGroupsY = (SCR_HEIGHT + (SCR_HEIGHT % TILE_SIZE)) / TILE_SIZE;

int V = 0;

enum class RenderingMode {
  rmDEFERRED,
  rmFORWARD,
};

RenderingMode mode = RenderingMode::rmFORWARD;

int main(int argc, char **argv) {
  // parameter
  // --------------------------------
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-deferred")) {
      mode = RenderingMode::rmDEFERRED;
    } else if (!strcmp(argv[i], "-forward")) {
      mode = RenderingMode::rmFORWARD;
    }
  }
  // glfw: initialize and configure
  // --------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  // glfw window creation
  // --------------------------------
  GLFWwindow *window =
      glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Forward+", NULL, NULL);
  if (window == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);

  // tell GLFW to capture our mouse
  // --------------------------------
  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // glad: load all OpenGL function pointers
  // --------------------------------
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  // tell stb_image.h to flip loaded texture's on the y-axis (before loading
  // model).
  // --------------------------------
  stbi_set_flip_vertically_on_load(false);

  // configure global opengl state
  // -----------------------------
  glEnable(GL_DEPTH_TEST);

  // build and compile shaders
  // -------------------------
  Shader sDeferred_GeometryPass("shader/GeometryPass.vs.glsl",
                                "shader/GeometryPass.fs.glsl");
  Shader sDeferred_LightingPass("shader/LightingPass.vs.glsl",
                                "shader/LightingPass.fs.glsl");
  Shader sForward_DepthPass("shader/ForwardDepthPass.vs.glsl",
                            "shader/ForwardDepthPass.fs.glsl");
  Shader sForward_LightingPass("shader/ForwardLightingPass.vs.glsl",
                               "shader/ForwardLightingPass.fs.glsl");
  Shader sComputePass("shader/LightCulling.cs.glsl");
  Shader sLightBox("shader/LightBox.vs.glsl", "shader/LightBox.fs.glsl");

  // load models
  // -----------
  Model backpack(std::filesystem::current_path().string() +
                 string("/resource/backpack/backpack.obj"));
  std::vector<glm::vec3> objectPositions;
  const int n = 5;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      objectPositions.push_back(glm::vec3(-3.0 * (n - 1) / 2 + i * 3.0, -0.5,
                                          -3.0 * (n - 1) / 2 + j * 3.0));
    }
  }

  // Resources
  // ------------------------------
  unsigned int gBuffer;
  unsigned int gPosition, gNormal, gAlbedoSpec, gDepth, dbgImage;
  unsigned int fBuffer;
  unsigned int fDepth;
  if (mode == RenderingMode::rmDEFERRED) {
    glGenFramebuffers(1, &gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
    // position color buffer
    glGenTextures(1, &gPosition);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0,
                 GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           gPosition, 0);
    // normal color buffer
    glGenTextures(1, &gNormal);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SCR_WIDTH, SCR_HEIGHT, 0,
                 GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           gNormal, 0);
    // color + specular color buffer
    glGenTextures(1, &gAlbedoSpec);
    glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                           gAlbedoSpec, 0);
    // depth buffer (used by compute shader)
    glGenTextures(1, &gDepth);
    glBindTexture(GL_TEXTURE_2D, gDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GLfloat borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           gDepth, 0);
    // tell OpenGL which color attachments we'll use (of this framebuffer) for
    // rendering
    unsigned int attachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                   GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, attachments);
  } else if (mode == RenderingMode::rmFORWARD) {
    glGenFramebuffers(1, &fBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
    // position color buffer
    glGenTextures(1, &fDepth);
    glBindTexture(GL_TEXTURE_2D, fDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    GLfloat borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           fDepth, 0);
  }
  // dbg image
  // glGenTextures(1, &dbgImage);
  // glBindTexture(GL_TEXTURE_2D, dbgImage);
  // glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, SCR_WIDTH, SCR_HEIGHT);
  // glBindImageTexture(0, dbgImage, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

  // // create and attach depth buffer (renderbuffer)
  // unsigned int rboDepth;
  // glGenRenderbuffers(1, &rboDepth);
  // glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
  // glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH,
  // SCR_HEIGHT); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
  // GL_RENDERBUFFER, rboDepth); finally check if framebuffer is complete
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cout << "Framebuffer not complete!" << std::endl;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // // configure depth fbo
  // // ------------------------------
  // unsigned int depthFbo;
  // glGenFramebuffers(1, &depthFbo);
  // glBindFramebuffer(GL_FRAMEBUFFER, depthFbo);
  // unsigned int depthMap;
  // // depth buffer
  // glGenTextures(1, &depthMap);
  // glBindTexture(GL_TEXTURE_2D, depthMap);
  // glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT,
  // 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL); glTexParameteri(GL_TEXTURE_2D,
  // GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,
  // GL_TEXTURE_MAG_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,
  // GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D,
  // GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER); GLfloat borderColor[] =
  // {1.0f, 1.0f, 1.0f, 1.0f}; glTexParameterfv(GL_TEXTURE_2D,
  // GL_TEXTURE_BORDER_COLOR, borderColor);
  // glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
  // depthMap, 0); unsigned int attachment_depthmap[] = {GL_COLOR_ATTACHMENT0};
  // glDrawBuffers(1, attachment_depthmap);

  // Random lighting
  // -------------------------------------------
  std::vector<Light> lights;
  std::srand(400);
  for (unsigned int i = 0; i < N_LIGHTS; i++) {
    Light newLight;
    // calculate slightly random offsets
    float xPos = ((rand() % 100) / 100.0) * n * 3.0 - 1.5 * n;
    float yPos = ((rand() % 100) / 100.0) * 3.0 - 1.5;
    float zPos = ((rand() % 100) / 100.0) * n * 3.0 - 1.5 * n;
    newLight.position = glm::vec4(xPos, yPos, zPos, 1.0);
    // also calculate random color
    float rColor = ((rand() % 100) / 200.0f) + 0.1;  // between 0.5 and 1.0
    float gColor = ((rand() % 100) / 200.0f) + 0.1;  // between 0.5 and 1.0
    float bColor = ((rand() % 100) / 200.0f) + 0.1;  // between 0.5 and 1.0
    newLight.color = glm::vec4(rColor, gColor, bColor, 1.0);
    newLight.radius = 2.0;
    lights.push_back(newLight);
  }

  // Create SSBOs
  // ----------------------------------------------------------------------------------
  GLuint ssboLightBuffer, ssboIndexBuffer;
  GLuint ssboDebugBuffer;  // for debug
  // Light buffer
  Light lightData[N_LIGHTS];
  for (int i = 0; i < N_LIGHTS; i++) {
    lightData[i].position = lights[i].position;
    lightData[i].color = lights[i].color;
    lightData[i].radius = lights[i].radius;
  }
  glGenBuffers(1, &ssboLightBuffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLightBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(lightData), lightData,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, ssboLightBuffer);
  Light *ptr = (Light *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  // Create buffer of visible light indices
  glGenBuffers(1, &ssboIndexBuffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboIndexBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               sizeof(int) * nWorkGroupsX * nWorkGroupsY * MAX_NLIGHTS_PER_TILE,
               NULL, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboIndexBuffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  // debug
  glGenBuffers(1, &ssboDebugBuffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebugBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               (sizeof(int) + sizeof(float) * 2) * nWorkGroupsX * nWorkGroupsY,
               NULL, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboDebugBuffer);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // render loop
  // -----------
  int nFrame = 0;
  float tF1 = 0;
  float tF2 = 0;
  float tF3 = 0;
  while (!glfwWindowShouldClose(window)) {
    // per-frame time logic
    // --------------------
    float currentFrame = glfwGetTime();
    float startTime, endTime;
    deltaTime = currentFrame - lastFrame;
    nFrame++;
    if (deltaTime >= 1) {
      glfwSetWindowTitle(window,
                         (to_string(nFrame) + " " + to_string(tF1) + "ms " +
                          to_string(tF2) + "ms " + to_string(tF3) + "ms")
                             .c_str());
      nFrame = 0;
      lastFrame = currentFrame;
    }

    // input
    // -----
    processInput(window);

    // render
    // ------
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (mode == RenderingMode::rmDEFERRED) {
      // 1. geometry pass: render scene's geometry/color data into gbuffer
      // -----------------------------------------------------------------
      startTime = glfwGetTime();
      glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glm::mat4 projection =
          glm::perspective(glm::radians(camera.Zoom),
                           (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
      glm::mat4 view = camera.GetViewMatrix();
      glm::mat4 model = glm::mat4(1.0f);
      sDeferred_GeometryPass.use();
      sDeferred_GeometryPass.setMat4("projection", projection);
      sDeferred_GeometryPass.setMat4("view", view);
      for (unsigned int i = 0; i < objectPositions.size(); i++) {
        model = glm::mat4(1.0f);
        model = glm::translate(model, objectPositions[i]);
        model = glm::scale(model, glm::vec3(1.0f));
        sDeferred_GeometryPass.setMat4("model", model);
        backpack.Draw(sDeferred_GeometryPass);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      endTime = glfwGetTime();
      tF1 = (endTime - startTime) * 1000;

      // 2. computing shader
      startTime = glfwGetTime();
      sComputePass.use();
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, gDepth);
      sComputePass.setInt("depthMap", 4);
      sComputePass.setInt("dbgMap", 0);
      sComputePass.setMat4("view", view);
      sComputePass.setMat4("projection", projection);
      glm::ivec2 screenSize(SCR_WIDTH, SCR_HEIGHT);
      sComputePass.setVec2i("screenSize", screenSize);
      sComputePass.setInt("N_LIGHTS", N_LIGHTS);

      glDispatchCompute(nWorkGroupsX, nWorkGroupsY, 1);
      glMemoryBarrier(GL_ALL_BARRIER_BITS);
      endTime = glfwGetTime();
      tF2 = (endTime - startTime) * 1000;

      // Read index (DEBUG)
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboDebugBuffer);
      DebugBuffer *ptrIndex =
          (DebugBuffer *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
      glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

      // 3. lighting pass: calculate lighting by iterating over a screen filled
      // quad pixel-by-pixel using the gbuffer's content.
      // -----------------------------------------------------------------------------------------------------------------------
      startTime = glfwGetTime();
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      sDeferred_LightingPass.use();
      sDeferred_LightingPass.setInt("gPosition", 0);
      sDeferred_LightingPass.setInt("gNormal", 1);
      sDeferred_LightingPass.setInt("gAlbedoSpec", 2);
      sDeferred_LightingPass.setInt("gDepth", 3);
      sDeferred_LightingPass.setInt("dbgMap", 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gPosition);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gNormal);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, gDepth);
      sDeferred_LightingPass.setVec3("viewPos", camera.Position);
      sDeferred_LightingPass.setInt("nTilesX", nWorkGroupsX);
      sDeferred_LightingPass.setInt("nTilesY", nWorkGroupsY);
      sDeferred_LightingPass.setInt("N_LIGHTS", N_LIGHTS);
      // debug
      sDeferred_LightingPass.setInt("V", V);
      // finally render quad
      renderQuad();
      endTime = glfwGetTime();
      tF3 = (endTime - startTime) * 1000;

      // 4.1. copy content of geometry's depth buffer to default framebuffer's
      // depth buffer
      // ----------------------------------------------------------------------------------
      glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH,
                        SCR_HEIGHT, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      // 4.2. render lights on top of scene
      // --------------------------------
      sLightBox.use();
      sLightBox.setMat4("projection", projection);
      sLightBox.setMat4("view", view);
      for (unsigned int i = 0; i < lights.size(); i++) {
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(lights[i].position));
        model = glm::scale(model, glm::vec3(0.0125f));
        sLightBox.setMat4("model", model);
        sLightBox.setVec3("lightColor", lights[i].color);
        renderCube();
      }
    } else if (mode == RenderingMode::rmFORWARD) {
      // 1. depth pass: render scene's depth data into texture
      // -----------------------------------------------------------------
      startTime = glfwGetTime();
      glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
      glClear(GL_DEPTH_BUFFER_BIT);
      glm::mat4 projection =
          glm::perspective(glm::radians(camera.Zoom),
                           (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
      glm::mat4 view = camera.GetViewMatrix();
      glm::mat4 model = glm::mat4(1.0f);
      sForward_DepthPass.use();
      sForward_DepthPass.setMat4("projection", projection);
      sForward_DepthPass.setMat4("view", view);
      for (unsigned int i = 0; i < objectPositions.size(); i++) {
        model = glm::mat4(1.0f);
        model = glm::translate(model, objectPositions[i]);
        model = glm::scale(model, glm::vec3(1.0f));
        sForward_DepthPass.setMat4("model", model);
        backpack.Draw(sForward_DepthPass);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      endTime = glfwGetTime();
      tF1 = (endTime - startTime) * 1000;

      // 2. computing shader
      // ------------------------------------------------------------------
      startTime = glfwGetTime();
      sComputePass.use();
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, fDepth);
      sComputePass.setInt("depthMap", 4);
      sComputePass.setInt("dbgMap", 0);
      sComputePass.setMat4("view", view);
      sComputePass.setMat4("projection", projection);
      glm::ivec2 screenSize(SCR_WIDTH, SCR_HEIGHT);
      sComputePass.setVec2i("screenSize", screenSize);
      sComputePass.setInt("N_LIGHTS", N_LIGHTS);

      glDispatchCompute(nWorkGroupsX, nWorkGroupsY, 1);
      glMemoryBarrier(GL_ALL_BARRIER_BITS);
      // glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLightBuffer);
      // Light *ptr = (Light *)glMapBuffer(GL_SHADER_STORAGE_BUFFER,
      // GL_READ_WRITE); glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
      // glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
      endTime = glfwGetTime();
      tF2 = (endTime - startTime) * 1000;

      // 3. lighting pass: calculate lighting by iterating over a screen filled
      // quad pixel-by-pixel using the gbuffer's content.
      // -----------------------------------------------------------------------------------------------------------------------
      startTime = glfwGetTime();
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      sForward_LightingPass.use();
      sForward_LightingPass.setInt("fDepth", 0);
      sForward_LightingPass.setInt("dbgMap", 0);
      sForward_LightingPass.setVec3("viewPos", camera.Position);
      sForward_LightingPass.setInt("nTilesX", nWorkGroupsX);
      sForward_LightingPass.setInt("nTilesY", nWorkGroupsY);
      sForward_LightingPass.setMat4("projection", projection);
      sForward_LightingPass.setMat4("view", view);
      sForward_LightingPass.setInt("V", V);
      sForward_LightingPass.setInt("N_LIGHTS", N_LIGHTS);
      for (unsigned int i = 0; i < objectPositions.size(); i++) {
        model = glm::mat4(1.0f);
        model = glm::translate(model, objectPositions[i]);
        model = glm::scale(model, glm::vec3(1.0f));
        sForward_LightingPass.setMat4("model", model);
        backpack.Draw(sForward_LightingPass);
      }
      endTime = glfwGetTime();
      tF3 = (endTime - startTime) * 1000;

      // 4.1. copy content of geometry's depth buffer to default framebuffer's
      // depth buffer
      // ----------------------------------------------------------------------------------
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fBuffer);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH,
                        SCR_HEIGHT, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      // 4.2. render lights on top of scene
      // --------------------------------
      sLightBox.use();
      sLightBox.setMat4("projection", projection);
      sLightBox.setMat4("view", view);
      for (unsigned int i = 0; i < lights.size(); i++) {
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(lights[i].position));
        model = glm::scale(model, glm::vec3(0.0125f));
        sLightBox.setMat4("model", model);
        sLightBox.setVec3("lightColor", lights[i].color);
        renderCube();
      }
    }

    // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved
    // etc.)
    // -------------------------------------------------------------------------------
    startTime = glfwGetTime();
    glfwSwapBuffers(window);
    glfwPollEvents();
    endTime = glfwGetTime();
    tF1 = (endTime - startTime) * 1000;
  }

  glfwTerminate();
  return 0;
}

// renderCube() renders a 1x1 3D cube in NDC.
// -------------------------------------------------
unsigned int cubeVAO = 0;
unsigned int cubeVBO = 0;
void renderCube() {
  // initialize (if necessary)
  if (cubeVAO == 0) {
    float vertices[] = {
        // back face
        -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,    // top-right
        1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,   // bottom-right
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,    // top-right
        -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
        -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,   // top-left
        // front face
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,   // bottom-right
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,    // top-right
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,    // top-right
        -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,   // top-left
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  // bottom-left
        // left face
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,    // top-right
        -1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,   // top-left
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // bottom-left
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // bottom-left
        -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // bottom-right
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,    // top-right
                                                             // right face
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,      // top-left
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,    // bottom-right
        1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,     // top-right
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,    // bottom-right
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,      // top-left
        1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,     // bottom-left
        // bottom face
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // top-right
        1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,   // top-left
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,    // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,    // bottom-left
        -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,   // bottom-right
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // top-right
        // top face
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top-left
        1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,    // bottom-right
        1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,   // top-right
        1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,    // bottom-right
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,  // top-left
        -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f    // bottom-left
    };
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    // fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    // link vertex attributes
    glBindVertexArray(cubeVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void *)(6 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
  }
  // render Cube
  glBindVertexArray(cubeVAO);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad() {
  if (quadVAO == 0) {
    float quadVertices[] = {
        // positions        // texture Coords
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
    };
    // setup plane VAO
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(3 * sizeof(float)));
  }
  glBindVertexArray(quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

// process all input: query GLFW whether relevant keys are pressed/released this
// frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window) {
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    camera.ProcessKeyboard(FORWARD, 0.01);
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    camera.ProcessKeyboard(BACKWARD, 0.01);
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    camera.ProcessKeyboard(LEFT, 0.01);
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    camera.ProcessKeyboard(RIGHT, 0.01);
  if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) V = 0;
  if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) V = 1;
  if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) V = 2;
  if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) V = 3;
}

// glfw: whenever the window size changed (by OS or user resize) this callback
// function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  // make sure the viewport matches the new window dimensions; note that width
  // and height will be significantly larger than specified on retina displays.
  glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow *window, double xpos, double ypos) {
  if (firstMouse) {
    lastX = xpos;
    lastY = ypos;
    firstMouse = false;
  }

  float xoffset = xpos - lastX;
  float yoffset =
      lastY - ypos;  // reversed since y-coordinates go from bottom to top

  lastX = xpos;
  lastY = ypos;

  camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  camera.ProcessMouseScroll(yoffset);
}
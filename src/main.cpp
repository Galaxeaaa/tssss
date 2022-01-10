#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#define _USE_MATH_DEFINES
#include <cmath>

#include "camera.hpp"
#include "model.hpp"
#include "shader.hpp"
#include "texture.hpp"

class GLTimer
{
public:
	GLTimer() : start(0), end(0)
	{
		glGenQueries(2, queryID);
	}
	void setStart()
	{
		glQueryCounter(queryID[0], GL_TIMESTAMP);
	};
	void setEnd()
	{
		glQueryCounter(queryID[1], GL_TIMESTAMP);
	};
	void wait()
	{
		GLint stop_timer_available = 0;
		while (!stop_timer_available)
		{
			glGetQueryObjectiv(queryID[1], GL_QUERY_RESULT_AVAILABLE, &stop_timer_available);
		}
	}
	float getTime_ms()
	{
		glGetQueryObjectui64v(queryID[0], GL_QUERY_RESULT, &start);
		glGetQueryObjectui64v(queryID[1], GL_QUERY_RESULT, &end);
		return (end - start) / 1000000.0;
	}

private:
	GLuint64 start, end;
	GLuint queryID[2];
};

struct Light
{
	glm::vec4 position;
	glm::vec4 color;
	float radius;
	float dummy[3]; // for alignment with std430
};

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);
unsigned int loadTexture(const char *path, bool gammaCorrection);
void renderQuad();
void renderCube();
GLenum glCheckError_(const char *file, int line);
#define glCheckError() glCheckError_(__FILE__, __LINE__)

// settings
// --------------------------------
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 800;

// camera
// --------------------------------
Camera camera(glm::vec3(0.0f, 0.0f, 1.0f));
float lastX = (float)SCR_WIDTH / 2.0;
float lastY = (float)SCR_HEIGHT / 2.0;
bool firstMouse = true;
const float move_speed = 0.01;

namespace tssss
{
	const unsigned int tex_w = 512;
	const unsigned int tex_h = 512;
	const unsigned int coef_w = 16;
	const unsigned int coef_h = 16;
}

enum class RenderingMode
{
	DEFERRED,
	FORWARD,
	SSS,
	HAAR,
};

RenderingMode mode = RenderingMode::SSS;

int main(int argc, char **argv)
{
	// parameter
	// --------------------------------
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-deferred"))
		{
			mode = RenderingMode::DEFERRED;
		}
		else if (!strcmp(argv[i], "-forward"))
		{
			mode = RenderingMode::FORWARD;
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

	// glfw: window creation and context setting
	// --------------------------------
	GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Forward+", NULL, NULL);
	if (window == NULL)
	{
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
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	// Check opengl environment parameters.
	// --------------------------------
	std::cout << glGetString(GL_VERSION) << std::endl;
	int workGroupSizes[3] = {0};
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSizes[0]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &workGroupSizes[1]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &workGroupSizes[2]);
	int workGroupCounts[3] = {0};
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &workGroupCounts[0]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &workGroupCounts[1]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &workGroupCounts[2]);
	int workGroupInvocations;
	glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &workGroupInvocations);
	std::cout << "workGroupSizes: [" << workGroupSizes[0] << ", " << workGroupSizes[1] << ", " << workGroupSizes[2] << "]" << std::endl;
	std::cout << "workGroupCounts: [" << workGroupCounts[0] << ", " << workGroupCounts[1] << ", " << workGroupCounts[2] << "]" << std::endl;
	std::cout << "workGroupInvocations: " << workGroupInvocations << std::endl;
	std::cout << "--------------------------------" << std::endl;

	// // tell stb_image.h to flip loaded texture's on the y-axis (before loading model).
	// // --------------------------------
	// stbi_set_flip_vertically_on_load(false);

	// configure global opengl state
	// --------------------------------
	glEnable(GL_DEPTH_TEST);

	// build and compile shaders
	// --------------------------------
	// - main passes
	Shader sHaarPass1("shader/HaarPass1.vs.glsl", "shader/HaarPass1.fs.glsl");
	Shader sHaarPass2("shader/HaarPass2.cs.glsl");
	Shader sRenderPass1("shader/RenderPass1.vs.glsl", "shader/RenderPass1.fs.glsl");
	Shader sRenderPass2("shader/RenderPass2.cs.glsl");
	Shader sRenderPass3("shader/RenderPass3.vs.glsl", "shader/RenderPass3.fs.glsl");
	// - verification tools
	Shader sCheckImage("shader/CheckImage.vs.glsl", "shader/CheckImage.fs.glsl");
	Shader sConvolveCoef("shader/ConvolveCoef.cs.glsl");
	Shader sInverseHaar("shader/InverseHaar.cs.glsl");

	// load models
	// --------------------------------
	Model smith(std::filesystem::current_path().string() + "/resource/smith/head.obj");
	// Model mike(std::filesystem::current_path().string() + "/resource/DigitalHuman.fbx");
	// Model backpack(std::filesystem::current_path().string() + string("/resource/backpack/backpack.obj"));

	// load textures
	// --------------------------------
	GLuint smith_diffuse = loadJPG("resource/smith/textures/lambertian.jpg");

	// Framebuffer and texture generation.
	// --------------------------------
	GLuint fBuffer;
	GLuint tssss_radiance_map, tssss_radiance_map_after_sss, tssss_world_pos_map, tssss_kernel, haar_wavelet_temp_image;
	if (mode == RenderingMode::HAAR)
	{
		// Framebuffer for pass 1
		glGenFramebuffers(1, &fBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
		// - world position map
		glGenTextures(1, &tssss_world_pos_map);
		glBindTexture(GL_TEXTURE_2D, tssss_world_pos_map);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tssss_world_pos_map, 0);
		GLuint attachments[1] = {GL_COLOR_ATTACHMENT0};
		glDrawBuffers(1, attachments);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// kernel buffer
		glGenTextures(1, &tssss_kernel);
		glBindTexture(GL_TEXTURE_2D, tssss_kernel);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		// temporary image during wavelet transformation
		glGenTextures(1, &haar_wavelet_temp_image);
		glBindTexture(GL_TEXTURE_2D, haar_wavelet_temp_image);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	}
	else if (mode == RenderingMode::SSS)
	{
		// Framebuffer for pass 1
		glGenFramebuffers(1, &fBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
		// - radiance map
		glGenTextures(1, &tssss_radiance_map);
		glBindTexture(GL_TEXTURE_2D, tssss_radiance_map);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tssss_radiance_map, 0);
		GLuint attachments[1] = {GL_COLOR_ATTACHMENT0};
		glDrawBuffers(1, attachments);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// temporary image during wavelet transformation
		glGenTextures(1, &haar_wavelet_temp_image);
		glBindTexture(GL_TEXTURE_2D, haar_wavelet_temp_image);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		// radiance map after sss
		glGenTextures(1, &tssss_radiance_map_after_sss);
		glBindTexture(GL_TEXTURE_2D, tssss_radiance_map_after_sss);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	}

	// Create SSBOs
	// --------------------------------
	GLuint ssbo_radiance_coef, ssbo_kernel_coef, ssbo_haar_mat1, ssbo_haar_mat2;
	if (mode == RenderingMode::HAAR)
	{
		glGenBuffers(1, &ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_radiance_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glGenBuffers(1, &ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	else if (mode == RenderingMode::SSS)
	{
		glGenBuffers(1, &ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_radiance_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glGenBuffers(1, &ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::tex_w * tssss::tex_h * tssss::coef_w * tssss::coef_h * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 model = glm::mat4(1.0f);
	model = glm::rotate(model, glm::float32(glm::radians(90.0)), glm::vec3(1.0, 0.0, 0.0));
	// model = glm::translate(model, glm::vec3(0, 60, -170));

	if (mode == RenderingMode::HAAR)
	{
		glm::mat4 model_haar;
		model_haar = glm::mat4(1.0f);
		model_haar = glm::translate(model_haar, glm::vec3(1.0, 1.0, 1.0));
		// Pass 1
		// --------------------------------
		// Render world position map into **tssss_world_pos_map**.
		// --------------------------------
		glViewport(0, 0, tssss::tex_w, tssss::tex_h); // Do this to render texture in a larger scale rather than size of display window.
		glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		sHaarPass1.use();
		sHaarPass1.setMat4("model", model_haar);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, smith_diffuse);
		smith.Draw(sHaarPass1);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Pass 2 Kernel
		// --------------------------------
		// Compute kernels.
		// --------------------------------
		// unsigned int row = 0;
		// unsigned int col = 0;
		GLTimer timer_haar;
		ofstream coef_file("test.sstx", ios::binary);
		for (int row = 0; row < tssss::tex_h; row++)
		{
			timer_haar.setStart();
			for (int col = 0; col < tssss::tex_w; col++)
			{
				sHaarPass2.use();
				sHaarPass2.setInt("coef_w", tssss::coef_w);
				sHaarPass2.setInt("coef_h", tssss::coef_h);
				sHaarPass2.setInt("tex_w", tssss::tex_w);
				sHaarPass2.setInt("tex_h", tssss::tex_h);
				glBindImageTexture(0, tssss_world_pos_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(1, tssss_kernel, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				glBindImageTexture(2, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
				sHaarPass2.setVec2i("index_kernel_iv", glm::ivec2(row, col));
				glDispatchCompute(1, 1, 1);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

				glfwSetWindowShouldClose(window, false);
				while (!glfwWindowShouldClose(window))
				{
					// process input
					// --------------------------------
					processInput(window);

					// Pass
					// --------------------------------
					// Check image.
					// --------------------------------
					glBindFramebuffer(GL_FRAMEBUFFER, 0);
					glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
					glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
					sCheckImage.use();
					// glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
					glBindImageTexture(1, tssss_world_pos_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
					glBindImageTexture(2, tssss_kernel, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
					renderQuad();

					glfwSwapBuffers(window);
					glfwPollEvents();
				}

				// Write to file.
				// --------------------------------
				glm::vec4 *kernel_coef_ptr = nullptr;
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
				kernel_coef_ptr = (glm::vec4 *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
				float *kernel_coef_float_ptr = new float[tssss::coef_h * tssss::coef_w];
				for (int i = 0; i < tssss::coef_h * tssss::coef_w; i++)
				{
					kernel_coef_float_ptr[i] = kernel_coef_ptr[i].r;
				}
				coef_file.write((char *)kernel_coef_float_ptr, tssss::coef_h * tssss::coef_w * sizeof(float));
				delete[] kernel_coef_float_ptr;
				glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			}
			timer_haar.setEnd();
			timer_haar.wait();
			printf("Time spent on row %d: %f ms\n", row, timer_haar.getTime_ms());
		}
		coef_file.close();
	}
	// Render loop
	// --------------------------------
	else if (mode == RenderingMode::SSS)
	{
		// // Read kernel haar coefficients from file and save to SSBO.
		// // --------------------------------
		// ifstream coef_file;
		// coef_file.open("test.sstx", ios::binary);
		// float *kernel_coef_buffer = new float[32 * 32];
		// glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		// glm::vec4 *kernel_coef_ptr = (glm::vec4 *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
		// for (int kernel_i = 0; kernel_i < tssss::tex_h * tssss::tex_w; kernel_i++)
		// {
		// 	coef_file.read((char *)kernel_coef_buffer, 32 * 32 * sizeof(float));
		// 	for (int row = 0; row < tssss::coef_h; row++)
		// 	{
		// 		for (int col = 0; col < tssss::coef_w; col++)
		// 		{
		// 			kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].r = kernel_coef_buffer[row * 32 + col];
		// 			kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].g = 0;
		// 			kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].b = 0;
		// 			kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].a = 0;
		// 		}
		// 	}
		// }
		// glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		// glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		// delete[] kernel_coef_buffer;
		// coef_file.close();

		while (!glfwWindowShouldClose(window))
		{
			// process input
			// --------------------------------
			processInput(window);

			// update matrices
			// --------------------------------
			projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100000.0f);
			view = camera.GetViewMatrix();

			GLTimer timer;
			// Pass 1
			// --------------------------------
			// Render radiance map into **tssss_radiance_map**.
			// --------------------------------
			// timer.setStart();
			glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
			glViewport(0, 0, tssss::tex_w, tssss::tex_h);
			sRenderPass1.use();
			sRenderPass1.setMat4("model", model);
			sRenderPass1.setMat4("view", view);
			sRenderPass1.setMat4("projection", projection);
			smith.Draw(sRenderPass1);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// timer.setEnd();
			// timer.wait();
			// printf("Pass 1 Radiance map: %fms. ", timer.getTime_ms());

			// Pass 2
			// --------------------------------
			// Compute haar transformation of radiance map.
			// --------------------------------
			// timer.setStart();
			sRenderPass2.use();
			sRenderPass2.setInt("coef_w", tssss::coef_w);
			sRenderPass2.setInt("coef_h", tssss::coef_h);
			sRenderPass2.setInt("tex_w", tssss::tex_w);
			sRenderPass2.setInt("tex_h", tssss::tex_h);
			glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glBindImageTexture(1, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glDispatchCompute(1, 1, 1);
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			// timer.setEnd();
			// timer.wait();
			// printf("Pass 2 Haar transform: %fms.\n", timer.getTime_ms());

			// Test
			// --------------------------------
			// Perform inverse haar transformation.
			// --------------------------------
			sInverseHaar.use();
			sInverseHaar.setInt("coef_w", tssss::coef_w);
			sInverseHaar.setInt("coef_h", tssss::coef_h);
			sInverseHaar.setInt("tex_w", tssss::tex_w);
			sInverseHaar.setInt("tex_h", tssss::tex_h);
			glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glBindImageTexture(1, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glDispatchCompute(1, 1, 1);
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

			// Pass
			// --------------------------------
			// Check radiance map and kernels.
			// --------------------------------
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			sCheckImage.use();
			glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glBindImageTexture(1, tssss_radiance_map_after_sss, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			renderQuad();

			// // Pass
			// // --------------------------------
			// // Convolve and output image.
			// // --------------------------------
			// timer.setStart();
			// sConvolveCoef.use();
			// sConvolveCoef.setInt("coef_w", tssss::coef_w);
			// sConvolveCoef.setInt("coef_h", tssss::coef_h);
			// sConvolveCoef.setInt("tex_w", tssss::tex_w);
			// sConvolveCoef.setInt("tex_h", tssss::tex_h);
			// glBindImageTexture(0, tssss_radiance_map_after_sss, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// glDispatchCompute(1, 1, 1);
			// glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			// timer.setEnd();
			// timer.wait();
			// printf("Convolution: %fms.\n", timer.getTime_ms());

			// // Pass 3
			// // --------------------------------
			// // Render.
			// // --------------------------------
			// glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			// glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			// sRenderPass3.use();
			// sRenderPass3.setMat4("model", model);
			// sRenderPass3.setMat4("view", view);
			// sRenderPass3.setMat4("projection", projection);
			// sRenderPass3.setVec3("view_pos", camera.Position);
			// glActiveTexture(GL_TEXTURE0);
			// glBindTexture(GL_TEXTURE_2D, smith_diffuse);
			// smith.Draw(sRenderPass3);

			glfwSwapBuffers(window);
			glfwPollEvents();
		}
	}
	else if (mode == RenderingMode::FORWARD)
	{
		while (!glfwWindowShouldClose(window))
		{
			// process input
			// --------------------------------
			processInput(window);

			// GLTimer timer;
			// // Pass 1
			// // --------------------------------
			// // Render radiance map into **tssss_radiance_map**.
			// // --------------------------------
			// timer.setStart();
			// glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
			// glViewport(0, 0, tssss::tex_w, tssss::tex_h);
			// sRenderPass1.use();
			// sRenderPass1.setMat4("model", model);
			// sRenderPass1.setMat4("view", view);
			// sRenderPass1.setMat4("projection", projection);
			// backpack.Draw(sRenderPass1);
			// glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// timer.setEnd();
			// timer.wait();
			// printf("Pass 1: %fms. ", timer.getTime_ms());

			// // Pass 2
			// // --------------------------------
			// // Compute haar transformation of radiance map.
			// // --------------------------------
			// timer.setStart();
			// sRenderPass2.use();
			// sRenderPass2.setInt("coef_w", tssss::coef_w);
			// sRenderPass2.setInt("coef_h", tssss::coef_h);
			// sRenderPass2.setInt("tex_w", tssss::tex_w);
			// sRenderPass2.setInt("tex_h", tssss::tex_h);
			// glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// glBindImageTexture(1, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// glDispatchCompute(1, 1, 1);
			// glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			// timer.setEnd();
			// timer.wait();
			// printf("Pass 2: %fms.\n", timer.getTime_ms());

			// // // Pass
			// // // --------------------------------
			// // // Convolve and output image.
			// // // --------------------------------
			// // timer.setStart();
			// // sConvolveCoef.use();
			// // sConvolveCoef.setInt("coef_w", tssss::coef_w);
			// // sConvolveCoef.setInt("coef_h", tssss::coef_h);
			// // sConvolveCoef.setInt("tex_w", tssss::tex_w);
			// // sConvolveCoef.setInt("tex_h", tssss::tex_h);
			// // glBindImageTexture(0, tssss_radiance_map_after_sss, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// // glDispatchCompute(1, 1, 1);
			// // glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			// // timer.setEnd();
			// // timer.wait();
			// // printf("Convolution: %fms.\n", timer.getTime_ms());

			// // Pass
			// // --------------------------------
			// // Check radiance map and kernels.
			// // --------------------------------
			// glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			// glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			// sCheckImage.use();
			// glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// glBindImageTexture(1, tssss_radiance_map_after_sss, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// renderQuad();

			// Pass 3
			// --------------------------------
			// Render.
			// --------------------------------
			glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			sRenderPass3.use();
			sRenderPass3.setMat4("model", model);
			sRenderPass3.setMat4("view", view);
			sRenderPass3.setMat4("projection", projection);
			sRenderPass3.setVec3("view_pos", camera.Position);
			smith.Draw(sRenderPass3);

			glfwSwapBuffers(window);
			glfwPollEvents();
		}
	}

	glfwTerminate();
	return 0;
}

// renderCube() renders a 1x1 3D cube in NDC.
// -------------------------------------------------
GLuint cubeVAO = 0;
GLuint cubeVBO = 0;
void renderCube()
{
	// initialize (if necessary)
	if (cubeVAO == 0)
	{
		float vertices[] = {
			// back face
			-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,	// top-right
			1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,	// bottom-right
			1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,	// top-right
			-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
			-1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,	// top-left
			// front face
			-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // bottom-left
			1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,  // bottom-right
			1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,	  // top-right
			1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,	  // top-right
			-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  // top-left
			-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // bottom-left
			// left face
			-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,	// top-right
			-1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,	// top-left
			-1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, // bottom-left
			-1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,	// bottom-right
			-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,	// top-right
																// right face
			1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,		// top-left
			1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,	// bottom-right
			1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,	// top-right
			1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,	// bottom-right
			1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,		// top-left
			1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,	// bottom-left
			// bottom face
			-1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, // top-right
			1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,	// top-left
			1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,	// bottom-left
			1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,	// bottom-left
			-1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,	// bottom-right
			-1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, // top-right
			// top face
			-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // top-left
			1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,	  // bottom-right
			1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,  // top-right
			1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,	  // bottom-right
			-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, // top-left
			-1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f	  // bottom-left
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
GLuint quadVAO = 0;
GLuint quadVBO = 0;
void renderQuad()
{
	if (quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,
			1.0f,
			0.0f,
			0.0f,
			1.0f,
			-1.0f,
			-1.0f,
			0.0f,
			0.0f,
			0.0f,
			1.0f,
			1.0f,
			0.0f,
			1.0f,
			1.0f,
			1.0f,
			-1.0f,
			0.0f,
			1.0f,
			0.0f,
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
void processInput(GLFWwindow *window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, move_speed);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, move_speed);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, move_speed);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, move_speed);
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		camera.ProcessKeyboard(UP, move_speed);
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		camera.ProcessKeyboard(DOWN, move_speed);
}

// glfw: whenever the window size changed (by OS or user resize) this callback
// function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width
	// and height will be significantly larger than specified on retina displays.
	glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow *window, double xpos, double ypos)
{
	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset =
		lastY - ypos; // reversed since y-coordinates go from bottom to top

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(yoffset);
}

// OpenGL error check function.
// --------------------------------
GLenum glCheckError_(const char *file, int line)
{
	GLenum errorCode;
	while ((errorCode = glGetError()) != GL_NO_ERROR)
	{
		std::string error;
		switch (errorCode)
		{
		case GL_INVALID_ENUM:
			error = "INVALID_ENUM";
			break;
		case GL_INVALID_VALUE:
			error = "INVALID_VALUE";
			break;
		case GL_INVALID_OPERATION:
			error = "INVALID_OPERATION";
			break;
		case GL_STACK_OVERFLOW:
			error = "STACK_OVERFLOW";
			break;
		case GL_STACK_UNDERFLOW:
			error = "STACK_UNDERFLOW";
			break;
		case GL_OUT_OF_MEMORY:
			error = "OUT_OF_MEMORY";
			break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			error = "INVALID_FRAMEBUFFER_OPERATION";
			break;
		}
		std::cout << error << " | " << file << " (" << line << ")" << std::endl;
	}
	return errorCode;
}
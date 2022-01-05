#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>

#include "camera.hpp"
#include "model.hpp"
#include "shader.hpp"

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

struct RGBA
{
	float data[4];
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
Camera camera(glm::vec3(0.0f, 0.0f, 5.0f));
float lastX = (float)SCR_WIDTH / 2.0;
float lastY = (float)SCR_HEIGHT / 2.0;
bool firstMouse = true;

namespace tssss
{
	const unsigned int tex_w = 1024;
	const unsigned int tex_h = 1024;
	const unsigned int coef_w = 32;
	const unsigned int coef_h = 32;
}

enum class RenderingMode
{
	DEFERRED,
	FORWARD,
	HAAR,
};

RenderingMode mode = RenderingMode::FORWARD;

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

	// tell stb_image.h to flip loaded texture's on the y-axis (before loading model).
	// --------------------------------
	stbi_set_flip_vertically_on_load(false);

	// configure global opengl state
	// --------------------------------
	glEnable(GL_DEPTH_TEST);

	// build and compile shaders
	// --------------------------------
	Shader sHaarPass1("shader/HaarPass1.vs.glsl", "shader/HaarPass1.fs.glsl");
	Shader sHaarPass2("shader/HaarPass2.cs.glsl");
	Shader sHaarPass2Test("shader/HaarPass2Test.cs.glsl");
	Shader sHaarPass2Kernel("shader/HaarPass2Kernel.cs.glsl");
	Shader sHaarPass3("shader/HaarPass3.vs.glsl", "shader/HaarPass3.fs.glsl");
	Shader sHaarPass4("shader/HaarPass4.cs.glsl");
	Shader sHaarPass5("shader/HaarPass5.vs.glsl", "shader/HaarPass5.fs.glsl");

	// load models
	// --------------------------------
	Model backpack(std::filesystem::current_path().string() + string("/resource/backpack/backpack.obj"));
	std::vector<glm::vec3> objectPositions;
	const int n = 5;
	for (int i = 0; i < n; i++)
	{
		for (int j = 0; j < n; j++)
		{
			objectPositions.push_back(glm::vec3(-3.0 * (n - 1) / 2 + i * 3.0, -0.5,
												-3.0 * (n - 1) / 2 + j * 3.0));
		}
	}

	// Framebuffer and texture generation.
	// --------------------------------
	GLuint fBuffer;
	GLuint tssss_radiance_map, tssss_world_pos_map, tssss_kernel, haar_wavelet_temp_image, tssss_radiance_map_after_sss;
	if (mode == RenderingMode::HAAR)
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
		// - world position map
		glGenTextures(1, &tssss_world_pos_map);
		glBindTexture(GL_TEXTURE_2D, tssss_world_pos_map);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tssss::tex_w, tssss::tex_h, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tssss_world_pos_map, 0);
		GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
		glDrawBuffers(2, attachments);
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

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Framebuffer not complete!" << std::endl;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	else if (mode == RenderingMode::FORWARD)
	{
		// Radiance map after convolution.
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
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(RGBA), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glGenBuffers(1, &ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(RGBA), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	else if (mode == RenderingMode::FORWARD)
	{
		glGenBuffers(1, &ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_radiance_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::coef_w * tssss::coef_h * sizeof(RGBA), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo_radiance_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glGenBuffers(1, &ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		glBufferData(GL_SHADER_STORAGE_BUFFER, tssss::tex_w * tssss::tex_h * tssss::coef_w * tssss::coef_h * sizeof(RGBA), nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_kernel_coef);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		glGetError();
	}

	glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
	glm::mat4 view = camera.GetViewMatrix();
	glm::mat4 model = glm::mat4(1.0f);

	if (mode == RenderingMode::HAAR)
	{
		// Pass 1
		// --------------------------------
		// Render radiance map into **tssss_radiance_map**.
		// Render world position map into **tssss_world_pos_map**.
		// --------------------------------
		glViewport(0, 0, tssss::tex_w, tssss::tex_h); // Do this to render texture in a larger scale rather than size of display window.
		glBindFramebuffer(GL_FRAMEBUFFER, fBuffer);
		glClear(GL_COLOR_BUFFER_BIT);
		sHaarPass1.use();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, backpack.textures_loaded[0].id);
		glUniform1i(glGetUniformLocation(sHaarPass1.ID, "texture_diffuse"), 0);
		sHaarPass1.setMat4("projection", projection);
		sHaarPass1.setMat4("view", view);
		model = glm::mat4(1.0f);
		// model = glm::translate(model, glm::vec3(1.0, 1.0, 1.0));
		// model = glm::scale(model, glm::vec3(1.0f));
		sHaarPass1.setMat4("model", model);
		backpack.Draw(sHaarPass1);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Pass 2
		// --------------------------------
		// Compute haar transformation of radiance map.
		// --------------------------------
		GLTimer timer_haar;
		ofstream coef_file("test.sstx", ios::binary);
		sHaarPass2.use();
		sHaarPass2.setInt("coef_w", tssss::coef_w);
		sHaarPass2.setInt("coef_h", tssss::coef_h);
		glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		timer_haar.setStart();
		glDispatchCompute(1, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
		timer_haar.setEnd();
		timer_haar.wait();
		printf("Time spent on radiance map: %f ms\n", timer_haar.getTime_ms());
		// - Write to file.
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_radiance_coef);
		RGBA *radiance_coef_ptr = (RGBA *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
		coef_file.write((char *)radiance_coef_ptr, tssss::coef_h * tssss::coef_w * sizeof(RGBA));
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		/* 	// Pass 2 Test
			// --------------------------------
			// Perform inverse haar transformation.
			// --------------------------------
			sHaarPass2Test.use();
			sHaarPass2Test.setInt("coef_w", tssss::coef_w);
			sHaarPass2Test.setInt("coef_h", tssss::coef_h);
			glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glBindImageTexture(1, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glDispatchCompute(1, 1, 1);
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT); */

		// Pass 2 Kernel
		// --------------------------------
		// Compute kernels.
		// --------------------------------
		sHaarPass2Kernel.use();
		sHaarPass2Kernel.setInt("coef_w", tssss::coef_w);
		sHaarPass2Kernel.setInt("coef_h", tssss::coef_h);
		sHaarPass2Kernel.setInt("tex_w", tssss::tex_w);
		sHaarPass2Kernel.setInt("tex_h", tssss::tex_h);
		glBindImageTexture(0, tssss_world_pos_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, tssss_kernel, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(2, haar_wavelet_temp_image, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		// unsigned int row = 0;
		// unsigned int col = 0;
		RGBA *kernel_coef_ptr;
		for (unsigned int row = 0; row < tssss::tex_h; row++)
		{
			for (unsigned int col = 0; col < tssss::tex_w; col++)
			{
				sHaarPass2Kernel.setVec2i("index_kernel_iv", glm::ivec2(row, col));
				glDispatchCompute(1, 1, 1);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			}
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
			kernel_coef_ptr = (RGBA *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
			float *kernel_coef_float_ptr = new float[tssss::coef_h * tssss::coef_w];
			for (int i = 0; i < tssss::coef_h * tssss::coef_w; i++)
				kernel_coef_float_ptr[i] = kernel_coef_ptr[i].data[0];
			coef_file.write((char *)kernel_coef_float_ptr, tssss::coef_h * tssss::coef_w * sizeof(float));
			delete[] kernel_coef_float_ptr;
			glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		}
		coef_file.close();
	}
	// Render loop
	// --------------------------------
	else if (mode == RenderingMode::FORWARD)
	{
		RGBA *radiance_coef_buffer = new RGBA[256 * 256];
		ifstream coef_file("test.sstx", ios::binary);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_radiance_coef);
		RGBA *radiance_coef_ptr = (RGBA *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
		// coef_file.read((char *)radiance_coef_ptr, tssss::coef_h * tssss::coef_w * sizeof(RGBA));
		coef_file.read((char *)radiance_coef_buffer, 256 * 256 * sizeof(RGBA));
		for (int row = 0; row < tssss::coef_h; row++)
		{
			for (int col = 0; col < tssss::coef_w; col++)
			{
				radiance_coef_ptr[row * tssss::coef_w + col] = radiance_coef_buffer[row * 256 + col];
			}
		}
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		delete[] radiance_coef_buffer;

		float *kernel_coef_buffer = new float[256 * 256];
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_kernel_coef);
		RGBA *kernel_coef_ptr = (RGBA *)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_WRITE);
		for (int kernel_i = 0; kernel_i < tssss::tex_h * tssss::tex_w; kernel_i++)
		{
			coef_file.read((char *)kernel_coef_buffer, 256 * 256 * sizeof(float));
			for (int row = 0; row < tssss::coef_h; row++)
			{
				for (int col = 0; col < tssss::coef_w; col++)
				{
					kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].data[0] = kernel_coef_buffer[row * 256 + col];
					kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].data[1] = 0;
					kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].data[2] = 0;
					kernel_coef_ptr[kernel_i * tssss::coef_w * tssss::coef_h + row * tssss::coef_w + col].data[3] = 0;
				}
			}
		}
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
		delete[] kernel_coef_buffer;

		coef_file.close();

		while (!glfwWindowShouldClose(window))
		{
			// process input
			// --------------------------------
			processInput(window);

			// // Pass 3
			// // --------------------------------
			// // Check radiance map and kernels.
			// // --------------------------------
			// glBindFramebuffer(GL_FRAMEBUFFER, 0);
			// glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			// glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			// sHaarPass3.use();
			// glBindImageTexture(0, tssss_radiance_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F); // MUST SET IN EACH PROGRAM
			// glBindImageTexture(1, tssss_world_pos_map, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// glBindImageTexture(2, tssss_kernel, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			// renderQuad();

			// Pass 4
			// --------------------------------
			// Construct convolved texture.
			// --------------------------------
			GLTimer timer;
			timer.setStart();
			sHaarPass4.use();
			sHaarPass4.setInt("coef_w", tssss::coef_w);
			sHaarPass4.setInt("coef_h", tssss::coef_h);
			sHaarPass4.setInt("tex_w", tssss::tex_w);
			sHaarPass4.setInt("tex_h", tssss::tex_h);
			glBindImageTexture(0, tssss_radiance_map_after_sss, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
			glDispatchCompute(1, 1, 1);
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
			timer.setEnd();
			timer.wait();
			printf("Convolve time: %fms.\n", timer.getTime_ms());

			// Pass 5
			// --------------------------------
			// Render.
			// --------------------------------
			glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			sHaarPass5.use();
			sHaarPass5.setMat4("model", model);
			sHaarPass5.setMat4("view", view);
			sHaarPass5.setMat4("projection", projection);
			sHaarPass5.setVec3("view_pos", camera.Position);
			glActiveTexture(GL_TEXTURE31);
			sHaarPass5.setInt("radiance_map_after_sss", 31);
			glBindTexture(GL_TEXTURE_2D, tssss_radiance_map_after_sss);
			backpack.Draw(sHaarPass5);

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
		camera.ProcessKeyboard(FORWARD, 0.01);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, 0.01);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, 0.01);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, 0.01);
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
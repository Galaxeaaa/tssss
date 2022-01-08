#pragma once
#pragma once
#include <glad/glad.h>
#include <string>
#include <iostream>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
using namespace std;
unsigned int TextureFromFile(const string &directory, bool gamma)
{
	std::string filename = directory;

	unsigned int textureID;
	glGenTextures(1, &textureID);

	int width, height, nrComponents;
	unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
	if (data)
	{
		GLenum format;
		if (nrComponents == 1)
			format = GL_RED;
		else if (nrComponents == 3)
			format = GL_RGB;
		else if (nrComponents == 4)
			format = GL_RGBA;

		glBindTexture(GL_TEXTURE_2D, textureID);
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		stbi_image_free(data);
	}
	else
	{
		std::cout << "Texture failed to load at path: " << filename << std::endl;
		stbi_image_free(data);
	}

	return textureID;
}

void loadData(unsigned char*& pdata, const char* imagepath, int& width, int& height, bool flip = false, bool isFloat = false)
{
	stbi_set_flip_vertically_on_load(flip);
	pdata = isFloat ? (unsigned char*)stbi_loadf(imagepath, &width, &height, nullptr, 4)
		: stbi_load(imagepath, &width, &height, nullptr, 4);

	if (nullptr == pdata)
		printf("%s  load texture error!!!!!!!!!!!!\n", imagepath);
}

int CalcMipmapLevel(int width, int height)
{
	int res;
	if (width > height) res = width; else res = height;
	int level = 1;
	if (res > 1)
		level = floor(std::log2(res)) + 1;
	return level;
}

vector<string> GenCubemapList(string a, string b)
{
	vector<string> n;
	for (int i = 0; i < 6; i++) {
		n.push_back(a + to_string(i) + b);
	}
	return n;
}

void LoadPrefilterEnvMap(int width, int height, GLuint envID, string basePath)
{

	int w = width, h = height;
	int ImageMip = CalcMipmapLevel(w, h);
	glBindTexture(GL_TEXTURE_CUBE_MAP, envID);
	for (int mip = 1; mip <= 5; mip++) {
		w /= 2; h /= 2;
		vector<unsigned char*> pdatas;
		pdatas.resize(6);
		for (int face = 0; face < 6; face++)
		{
			string ImageName = basePath + "_mip" + to_string(mip) + "_face" + to_string(face) + ".hdr";
			loadData(pdatas[face], ImageName.c_str(), width, height, false, true);
			glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, 0, 0, w, h, GL_RGBA, GL_FLOAT, pdatas[face]);
		}

	}
}

GLuint CreateTextureCubeMap(const vector<string>& paths, int& width, int& height)
{
	vector<unsigned char*> pdatas;
	pdatas.resize(6);
	width = 0, height = 0;
	for (int i = 0; i < paths.size(); ++i)
	{
		loadData(pdatas[i], paths[i].c_str(), width, height, true, true);
	}

	GLuint texID = 0;
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texID);
	int mipmap_level = CalcMipmapLevel(width, height);
	glTexStorage2D(GL_TEXTURE_CUBE_MAP, mipmap_level, GL_RGBA32F, width, height);
	for (int i = 0; i < 6; i++) {
		glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, &pdatas[i][0]);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	return texID;
}

GLuint CreateTextureCubeMap_MipMap(const vector<string>& paths)
{
	int width = 0, height = 0;
	auto texID = CreateTextureCubeMap(paths, width, height);

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// feed mip data
	LoadPrefilterEnvMap(width, height, texID, "pbr/pisa");

	return texID;
}

GLuint loadJPG(const char* imagepath, bool flip = false, bool mipmap = false)
{
	int width, height;
	unsigned char* pdata;
	loadData(pdata, imagepath, width, height, flip, false);

	int mipmapLevel = 1;
	if (mipmap)
		mipmapLevel = CalcMipmapLevel(width, height);

	GLuint texID = 0;
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexStorage2D(GL_TEXTURE_2D, mipmapLevel, GL_RGBA8, width, height);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pdata);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (!mipmap)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	if (mipmap)
		glGenerateMipmap(GL_TEXTURE_2D);

	return texID;
}

GLuint loadBMP_custom(const char * imagepath) {
	printf("Reading image %s\n", imagepath);

	// Data read from the header of the BMP file
	unsigned char header[54];
	unsigned int dataPos;
	unsigned int imageSize;
	unsigned int width, height;
	// Actual RGB data
	unsigned char * data;

	// Open the file
	FILE * file = fopen(imagepath, "rb");
	if (!file) { printf("%s could not be opened. Are you in the right directory ? Don't forget to read the FAQ !\n", imagepath); getchar(); return 0; }

	// Read the header, i.e. the 54 first bytes

	// If less than 54 bytes are read, problem
	if (fread(header, 1, 54, file) != 54) {
		printf("Not a correct BMP file\n");
		return 0;
	}
	// A BMP files always begins with "BM"
	if (header[0] != 'B' || header[1] != 'M') {
		printf("Not a correct BMP file\n");
		return 0;
	}
	// Make sure this is a 24bpp file
	if (*(int*)&(header[0x1E]) != 0) { printf("Not a correct BMP file\n");    return 0; }
	if (*(int*)&(header[0x1C]) != 24) { printf("Not a correct BMP file\n");    return 0; }

	// Read the information about the image
	dataPos = *(int*)&(header[0x0A]);
	imageSize = *(int*)&(header[0x22]);
	width = *(int*)&(header[0x12]);
	height = *(int*)&(header[0x16]);

	// Some BMP files are misformatted, guess missing information
	if (imageSize == 0)    imageSize = width * height * 3; // 3 : one byte for each Red, Green and Blue component
	if (dataPos == 0)      dataPos = 54; // The BMP header is done that way

										 // Create a buffer
	data = new unsigned char[imageSize];

	// Read the actual data from the file into the buffer
	fread(data, 1, imageSize, file);

	// Everything is in memory now, the file wan be closed
	fclose(file);

	// Create one OpenGL texture
	GLuint textureID;
	glGenTextures(1, &textureID);

	// "Bind" the newly created texture : all future texture functions will modify this texture
	glBindTexture(GL_TEXTURE_2D, textureID);

	// Give the image to OpenGL
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_BGR, GL_UNSIGNED_BYTE, data);

	// OpenGL has now copied the data. Free our own version
	delete[] data;

	// Poor filtering, or ...
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 

	// ... nice trilinear filtering.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Return the ID of the texture we just created
	return textureID;
}

GLuint loadTGA(const char* imagepath)
{
	int width, height;
	unsigned char* pdata;
	loadData(pdata, imagepath, width, height, false, true);

	GLuint texID = 0;
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, width, height);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, pdata);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	return texID;
}
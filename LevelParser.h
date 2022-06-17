#ifndef __LEVELPARSER_H__
#define __LEVELPARSER_H__
#include <fstream>
#include "h2bParser.h"
#include <iostream>
#include <unordered_map>
#include <vector>
#include "../Gateware/Gateware/Gateware.h"

/**
 * Trim solution found from:
 * https://stackoverflow.com/questions/216823/how-to-trim-a-stdstring
 */

 // trim from start (in place)
static inline void trimLeft(std::string& str) {
	str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
		return !std::isspace(ch);
		}));
}

// trim from end (in place)
static inline void trimRight(std::string& str) {
	str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
		}).base(), str.end());
}

// trim from both ends (in place)
static inline void trim(std::string& str) {
	trimLeft(str);
	trimRight(str);
}

namespace LevelParser
{
	const int ERR_OPENING_FILE = 1;
	const int ERR_MALFORMED_FILE = 2;
	const int ERR_MODEL_FILE_PATH = 3;
	const int OK = 0;

	extern const char* modelAssetPath;
	extern const char* moelAssetExt;

	class Parser
	{
		std::ifstream fileHandler;
		H2B::Parser h2bParser;
		std::string line2Parse;

		void Clear();

		// Error Functions
		int ErrOpeningFile();
		int ErrMalformedFile();
		int ErrFindingModelFile(std::string& filePath);

		// Load Handlers
		int LoadMesh(std::string meshName);
		int LoadCamera(std::string cameraName);
		int LoadLight(const char* lightFile);
		int ParseMatrix(GW::MATH::GMATRIXF& matrix);
		int ParseMatrixLine(GW::MATH::GMATRIXF& matrix, int offset);

		// Name Parser
		std::string GetMeshNameFromLine();

	public:
		std::unordered_map<std::string, graphics::MODEL> models;
		std::unordered_map<std::string, graphics::CAMERA> cameras;
		std::unordered_map<std::string, graphics::LIGHT> lights;
		unsigned int modelCount;
		unsigned int cameraCount;
		unsigned int lightCount;

		int ParseGameLevel(const char* filePath);
		std::vector<graphics::MODEL> ModelsToVector();
		std::vector<graphics::CAMERA> CamerasToVector();
		
	};
}

#endif

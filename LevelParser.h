#ifndef __LEVELPARSER_H__
#define __LEVELPARSER_H__
#include <fstream>
#include "h2bParser.h"
#include <iostream>
#include <unordered_map>
#include <vector>
#include "../Gateware/Gateware/Gateware.h"

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
		int LoadMesh(std::string& meshFileName);
		int LoadCamera(const char* cameraFile);
		int LoadLight(const char* lightFile);
		int ParseMatrix(std::string tag);
		int ParseMatrixLine(GW::MATH::GMATRIXF& matrix, int offset);

	public:
		std::unordered_map<std::string, graphics::MODEL> models;
		std::unordered_map<std::string, std::vector<GW::MATH::GMATRIXF>*> modelPositions;
		unsigned int modelCount;
		unsigned int lightCount;

		int ParseGameLevel(const char* filePath);
		
	};
}

#endif

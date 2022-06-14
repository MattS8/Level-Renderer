#include "LevelParser.h"
#include <algorithm> 
#include <cctype>
#include <locale>

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

const char* LevelParser::modelAssetPath = "../Model Assets/H2B/";
const char* LevelParser::moelAssetExt = ".h2b";

int LevelParser::Parser::ParseGameLevel(const char* filePath)
{
	// Clear Old Data
	LevelParser::Parser::Clear();

	// Open File
	fileHandler.open(filePath, std::ifstream::in);

	if (!fileHandler.is_open())
		return ErrOpenigFile();

	// Read Each Line
	int handlerReturnVal;
	while (std::getline(fileHandler, line2Parse))
	{
		trim(line2Parse);

		if (line2Parse.at(0) == '#')
			continue;

		// Handle Mesh Object
		if (std::strcmp(line2Parse.c_str(), "MESH") == 0)
		{
			if (!std::getline(fileHandler, line2Parse))
				return ErrMalformedFile();
			if (LoadMesh(line2Parse) != LevelParser::OK)
				return ErrMalformedFile();
			if (ParseMatrix(line2Parse) != LevelParser::OK)
				return ErrMalformedFile();
		}
		//// Handle Camera Object
		//else if (std::strcmp(line2Parse.c_str(), "CAMERA") == 0)
		//{
		//	if (!std::getline(fileHandler, line2Parse))
		//		return ErrMalformedFile();
		//	if (LoadCamera(line2Parse.c_str()) != LevelParser::OK)
		//		return ErrMalformedFile();
		//	if (ParseMatrix(line2Parse) != LevelParser::OK)
		//		return ErrMalformedFile();
		//}
		//// Handle Light Object
		//else if (std::strcmp(line2Parse.c_str(), "LIGHT") == 0)
		//{
		//	if (!std::getline(fileHandler, line2Parse))
		//		return ErrMalformedFile();
		//	if (LoadLight(line2Parse.c_str()) != LevelParser::OK)
		//		return ErrMalformedFile();
		//	if (ParseMatrix(line2Parse) != LevelParser::OK)
		//		return ErrMalformedFile();
		//}
	}
}

// Loaders

int LevelParser::Parser::ParseMatrixLine(GW::MATH::GMATRIXF& matrix, int offset)
{
	static const char* matrixFirstLine = " <Matrix 4x4 (%f, %f, %f, %f";
	static const char* matrixOtherLine = "            (%f, %f, %f, %f";

	int numItemsScanned;
	if (!std::getline(fileHandler, line2Parse))
		return LevelParser::ERR_MALFORMED_FILE;

	numItemsScanned = std::sscanf(line2Parse.c_str(),
		offset == 0 ? matrixFirstLine : matrixOtherLine,
		&matrix.data[(offset * 4) + 0],
		&matrix.data[(offset * 4) + 1],
		&matrix.data[(offset * 4) + 2],
		&matrix.data[(offset * 4) + 3]);

	return numItemsScanned == 4 
		? LevelParser::OK 
		: LevelParser::ERR_MALFORMED_FILE;
}

int LevelParser::Parser::ParseMatrix(std::string tag)
{
	GW::MATH::GMATRIXF matrix;

	// Parse matrix line-by-line
	int retVal;
	for (int i = 0; i < 4; i++)
	{
		if (retVal = ParseMatrixLine(matrix, i) != LevelParser::OK)
			return retVal;
	}

	// Add matrix to list
	auto positionsList = modelPositions.find(tag);
	if (positionsList == modelPositions.end())
	{
		std::vector<GW::MATH::GMATRIXF>* newPosList = new std::vector<GW::MATH::GMATRIXF>();
		newPosList->push_back(matrix);
		modelPositions[tag] = newPosList;
	}
	else
	{
		positionsList->second->push_back(matrix);
	}

	return LevelParser::OK;
}

int LevelParser::Parser::LoadMesh(std::string& meshName)
{
	int dotPos = meshName.find('.');
	if (dotPos != std::string::npos)
		meshName = meshName.substr(0, dotPos);

	auto modelItter = models.find(meshName);
	if (modelItter != models.end())
		return LevelParser::OK;

	std::string fullPath = std::string(modelAssetPath) 
		+ meshName 
		+ moelAssetExt;
	if (!h2bParser.Parse(fullPath.c_str()))
		return ErrFindingModelFile(fullPath);
	
	models[meshName] = h2bParser.model;

	return LevelParser::OK;
}

int LevelParser::Parser::LoadCamera(const char* cameraFile)
{
	return LevelParser::OK;
}

int LevelParser::Parser::LoadLight(const char* lightFile)
{
	return LevelParser::OK;
}

// Error Functions

int LevelParser::Parser::ErrFindingModelFile(std::string& filePath)
{
	std::cerr << "Could not open file: " << filePath << "\n";
	return LevelParser::ERR_OPENING_FILE;
}

int LevelParser::Parser::ErrMalformedFile()
{
	std::cerr << "GameLevel file was malformed.\n";
	return LevelParser::ERR_MALFORMED_FILE;
}

int LevelParser::Parser::ErrOpenigFile()
{
	std::cerr << "Failed to open file.\n";
	return LevelParser::ERR_OPENING_FILE;
}

// Manager Functions

void LevelParser::Parser::Clear()
{
	for (auto it = modelPositions.begin(); it != modelPositions.end(); it++)
		delete it->second;

	modelPositions.clear();
	models.clear();
}
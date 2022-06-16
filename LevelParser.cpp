#include "LevelParser.h"
#include <algorithm> 
#include <cctype>
#include <locale>

const char* LevelParser::modelAssetPath = "../Model Assets/H2B/";
const char* LevelParser::moelAssetExt = ".h2b";

std::vector<graphics::MODEL> LevelParser::Parser::ModelsToVector()
{
	std::vector<graphics::MODEL> modelsVector;
	modelsVector.reserve(models.size());

	for (auto itter = models.begin(); itter != models.end(); itter++)
	{
		modelsVector.push_back(itter->second);
	}

	return modelsVector;
}

int LevelParser::Parser::ParseGameLevel(const char* filePath)
{
	// Clear Old Data
	LevelParser::Parser::Clear();

	// Open File
	fileHandler.open(filePath, std::ifstream::in);

	if (!fileHandler.is_open())
		return ErrOpeningFile();

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

			std::string meshName = GetMeshNameFromLine();

			if (LoadMesh(meshName) != LevelParser::OK)
				return ErrMalformedFile();
		}
		// Handle Camera Object
		else if (std::strcmp(line2Parse.c_str(), "CAMERA") == 0)
		{
			if (!std::getline(fileHandler, line2Parse))
				return ErrMalformedFile();
			if (LoadCamera(line2Parse) != LevelParser::OK)
				return ErrMalformedFile();
		}
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

	modelCount = models.size();
	lightCount = 0;		// TODO: Change to proper size when implemented

	return LevelParser::OK;
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

int LevelParser::Parser::ParseMatrix(GW::MATH::GMATRIXF& matrix)
{
	// Parse matrix line-by-line
	int retVal;
	for (int i = 0; i < 4; i++)
	{
		if (retVal = ParseMatrixLine(matrix, i) != LevelParser::OK)
			return retVal;
	}

	return LevelParser::OK;
}

std::string LevelParser::Parser::GetMeshNameFromLine()
{
	int dotPos = line2Parse.find('.');
	
	return dotPos != std::string::npos 
		? line2Parse.substr(0, dotPos)
		: std::string(line2Parse);
}

int LevelParser::Parser::LoadMesh(std::string meshName)
{
	if (models.find(meshName) == models.end())
	{
		std::string fullPath = std::string(modelAssetPath)
			+ meshName
			+ moelAssetExt;
		if (!h2bParser.Parse(fullPath.c_str()))
			return ErrFindingModelFile(fullPath);

		h2bParser.model.instanceCount = 1;
		models[meshName] = h2bParser.model;
	}
	else
	{
		models[meshName].instanceCount += 1;
	}

	GW::MATH::GMATRIXF newMatrix;
	int retVal = ParseMatrix(newMatrix);
	if (retVal != LevelParser::OK)
		return retVal;

	// Add matrix to list
	models[meshName].worldMatrices.push_back(newMatrix);

	//graphics::MODEL* newModel = new graphics::MODEL();
	//memcpy(newModel, &(h2bParser.model), sizeof(graphics::MODEL));
	//newModel->instanceCount = 1;
	//models[meshName] = newModel;

	return LevelParser::OK;
}

int LevelParser::Parser::LoadCamera(std::string cameraName)
{
	static const char* CameraFOVLine = "<FOV %f >";
	static const char* CameraNearPlaneLine = "<Near %f >";
	static const char* CameraFarPlaneLine = "<Far %f >";

	if (cameras.find(cameraName) != cameras.end())
	{
		std::cout << "Level Parser - WARNING: Already found camera with name '" << cameraName << "'. The only camera will be overwritten!\n";
	}

	graphics::CAMERA newCamera;
	ParseMatrix(newCamera.worldMatrix);
	
	int numItemsScanned;

	// Parse FOV
	if (!std::getline(fileHandler, line2Parse))
		return LevelParser::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraFOVLine, &(newCamera.FOV));

	if (numItemsScanned != 1)
		return LevelParser::ERR_MALFORMED_FILE;

	// Parse Near Plane
	if (!std::getline(fileHandler, line2Parse))
		return LevelParser::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraNearPlaneLine, &(newCamera.nearPlane));

	if (numItemsScanned != 1)
		return LevelParser::ERR_MALFORMED_FILE;
	
	// Parse Far Plane 
	if (!std::getline(fileHandler, line2Parse))
		return LevelParser::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraFarPlaneLine, &(newCamera.farPlane));

	if (numItemsScanned != 1)
		return LevelParser::ERR_MALFORMED_FILE;

	cameras[cameraName] = newCamera;

	return LevelParser::OK;
}

int LevelParser::Parser::LoadLight(const char* lightFile)
{
	return LevelParser::OK;
}

// Error Functions

int LevelParser::Parser::ErrFindingModelFile(std::string& filePath)
{
	std::cerr << "Level Parser - ERROR: Could not open file: " << filePath << "\n";
	return LevelParser::ERR_OPENING_FILE;
}

int LevelParser::Parser::ErrMalformedFile()
{
	std::cerr << "Level Parser - ERROR: GameLevel file was malformed.\n";
	return LevelParser::ERR_MALFORMED_FILE;
}

int LevelParser::Parser::ErrOpeningFile()
{
	std::cerr << "Level Parser - ERROR: Failed to open file.\n";
	return LevelParser::ERR_OPENING_FILE;
}

// Manager Functions

void LevelParser::Parser::Clear()
{
	models.clear();
	cameras.clear();
	lights.clear();
}
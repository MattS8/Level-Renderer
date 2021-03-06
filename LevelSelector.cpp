#include "LevelSelector.h"
#include <algorithm> 
#include <cctype>
#include <locale>
#include <windows.h>
#include <Commdlg.h>

bool LevelSelector::Selector::SelectNewLevel(bool showPrompt = false)
{

	if (showPrompt)
		MessageBox(NULL, L"Levels can be loaded by pressing the 'F1' key. \n\nCamera Controls: WASD\n\nLight Controls NUM Pad 4(left) 5(backwards) 6(right) 8(forwards) +(up) enter(down)\n\nReset the light using NUM Pad 0.\n\n",
			L"Level Selection",
			MB_OK);

	OPENFILENAME ofn;
	wchar_t szFile[MAX_PATH];
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"All\0*.txt\0*.lvl\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	GetOpenFileName(&ofn);

	std::wstring ws(szFile);
	std::string fileStr = std::string(ws.begin(), ws.end());

	bool newFileSelected = fileStr.compare("") != 0;

	if (newFileSelected)
		selectedFile = fileStr;

	return newFileSelected;
}

const char* LevelSelector::modelAssetPath = "../Assets/Models/";
const char* LevelSelector::textureAssetPath = "../Assets/Textures/";
const char* LevelSelector::textureExt = ".ktx";
const char* LevelSelector::modelAssetExt = ".h2b";

// Main Parse Function

int LevelSelector::Parser::ParseGameLevel(std::string& filePath)
{
	// Clear Old Data
	LevelSelector::Parser::Clear();

	// Open File
	fileHandler.open(filePath.c_str(), std::ifstream::in);

	if (!fileHandler.is_open())
		return ErrOpeningFile();

	// Read Each Line
	int handlerReturnVal;
	while (std::getline(fileHandler, line2Parse))
	{
		trim(line2Parse);

		if (line2Parse.size() == 0 || line2Parse.at(0) == '#')
			continue;

		// Handle Mesh Object
		if (std::strcmp(line2Parse.c_str(), "MESH") == 0)
		{
			if (!std::getline(fileHandler, line2Parse))
				return ErrMalformedFile();

			std::string meshName = GetNameFromLine();

			if (LoadMesh(meshName) != LevelSelector::OK)
				return ErrMalformedFile();
		}
		// Handle Camera Object
		else if (std::strcmp(line2Parse.c_str(), "CAMERA") == 0)
		{
			if (!std::getline(fileHandler, line2Parse))
				return ErrMalformedFile();
			if (LoadCamera(line2Parse) != LevelSelector::OK)
				return ErrMalformedFile();
		}
		//// Handle Light Object
		//else if (std::strcmp(line2Parse.c_str(), "LIGHT") == 0)
		//{
		//	if (!std::getline(fileHandler, line2Parse))
		//		return ErrMalformedFile();
		//	if (LoadLight(line2Parse.c_str()) != LevelSelector::OK)
		//		return ErrMalformedFile();
		//	if (ParseMatrix(line2Parse) != LevelSelector::OK)
		//		return ErrMalformedFile();
		//}
	}

	modelCount = models.size();
	cameraCount = cameras.size();
	lightCount = 0;		// TODO: Change to proper size when implemented

	fileHandler.close();

	return LevelSelector::OK;
}

// Data Conversions

std::vector<graphics::MODEL> LevelSelector::Parser::ModelsToVector()
{
	std::vector<graphics::MODEL> modelsVector;
	modelsVector.reserve(models.size());

	for (auto itter = models.begin(); itter != models.end(); itter++)
	{
		modelsVector.push_back(itter->second);
	}

	return modelsVector;
}

std::vector<graphics::CAMERA> LevelSelector::Parser::CamerasToVector()
{
	std::vector<graphics::CAMERA> camerasVector;
	camerasVector.reserve(cameras.size());

	for (auto itter = cameras.begin(); itter != cameras.end(); itter++)
	{
		camerasVector.push_back(itter->second);
	}

	return camerasVector;
}


// Private Loaders

int LevelSelector::Parser::ParseMatrixLine(GW::MATH::GMATRIXF& matrix, int offset)
{
	static const char* matrixFirstLine = " <Matrix 4x4 (%f, %f, %f, %f";
	static const char* matrixOtherLine = "            (%f, %f, %f, %f";

	int numItemsScanned;
	if (!std::getline(fileHandler, line2Parse))
		return LevelSelector::ERR_MALFORMED_FILE;

	numItemsScanned = std::sscanf(line2Parse.c_str(),
		offset == 0 ? matrixFirstLine : matrixOtherLine,
		&matrix.data[(offset * 4) + 0],
		&matrix.data[(offset * 4) + 1],
		&matrix.data[(offset * 4) + 2],
		&matrix.data[(offset * 4) + 3]);

	return numItemsScanned == 4 
		? LevelSelector::OK 
		: LevelSelector::ERR_MALFORMED_FILE;
}

int LevelSelector::Parser::ParseMatrix(GW::MATH::GMATRIXF& matrix)
{
	// Parse matrix line-by-line
	int retVal;
	for (int i = 0; i < 4; i++)
	{
		if (retVal = ParseMatrixLine(matrix, i) != LevelSelector::OK)
			return retVal;
	}

	return LevelSelector::OK;
}

int LevelSelector::Parser::LoadMesh(std::string meshName)
{
	if (models.find(meshName) == models.end())
	{
		h2bParser.Clear();
		std::string fullPath = std::string(modelAssetPath)
			+ meshName
			+ modelAssetExt;
		
		if (!h2bParser.Parse(fullPath.c_str()))
			return ErrFindingModelFile(fullPath);

		h2bParser.model.instanceCount = 1;
		ParseMaterials();

		h2bParser.model.modelName = meshName;
		models[meshName] = h2bParser.model;
	}
	else
	{
		models[meshName].instanceCount += 1;
	}

	GW::MATH::GMATRIXF newMatrix;
	int retVal = ParseMatrix(newMatrix);
	if (retVal != LevelSelector::OK)
		return retVal;

	// Add matrix to list
	models[meshName].worldMatrices.push_back(newMatrix);

	return LevelSelector::OK;
}

int LevelSelector::Parser::LoadCamera(std::string cameraName)
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
		return LevelSelector::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraFOVLine, &(newCamera.FOV));

	if (numItemsScanned != 1)
		return LevelSelector::ERR_MALFORMED_FILE;

	// Parse Near Plane
	if (!std::getline(fileHandler, line2Parse))
		return LevelSelector::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraNearPlaneLine, &(newCamera.nearPlane));

	if (numItemsScanned != 1)
		return LevelSelector::ERR_MALFORMED_FILE;
	
	// Parse Far Plane 
	if (!std::getline(fileHandler, line2Parse))
		return LevelSelector::ERR_MALFORMED_FILE;
	numItemsScanned = sscanf(line2Parse.c_str(), CameraFarPlaneLine, &(newCamera.farPlane));

	if (numItemsScanned != 1)
		return LevelSelector::ERR_MALFORMED_FILE;

	cameras[cameraName] = newCamera;

	return LevelSelector::OK;
}

void LevelSelector::Parser::ParseMaterials()
{
	// Add up number of Diffuse, Specular, and Normal Materials
	for (graphics::MATERIAL& mat : h2bParser.model.materials)
	{
		levelInfo.totalMaterialCount += 1;
		// Check for Diffuse Map
		if (mat.map_Kd != nullptr)
		{
			h2bParser.model.diffuseTextures.push_back(FormatTexturePath(mat.map_Kd));

			h2bParser.model.materialInfo.diffuseCount += 1;
			levelInfo.totalDiffuseCount += 1;
		}
		else
			h2bParser.model.diffuseTextures.push_back("");

		// Check for Specular Map
		if (mat.map_Ks != nullptr)
		{
			h2bParser.model.specularTextures.push_back(FormatTexturePath(mat.map_Ks));

			h2bParser.model.materialInfo.specularCount += 1;
			levelInfo.totalSpecularCount += 1;
		}
		else
			h2bParser.model.specularTextures.push_back("");

		// Check for Normal Map
		if (mat.map_Ns != nullptr)
		{
			h2bParser.model.normalTextures.push_back(FormatTexturePath(mat.map_Ns));

			h2bParser.model.materialInfo.normalCount += 1;
			levelInfo.totalNormalCount += 1;
		}
		else
			h2bParser.model.normalTextures.push_back("");
	}
}

// String Parsers
std::string LevelSelector::Parser::GetNameFromLine()
{
	int dotPos = line2Parse.find('.');

	return dotPos != std::string::npos
		? line2Parse.substr(0, dotPos)
		: std::string(line2Parse);
}

std::string LevelSelector::Parser::FormatTexturePath(const char* filePath)
{
	std::string formattedName(filePath);
	int relativePathLocation = formattedName.find_last_of('/');
	if (relativePathLocation != std::string::npos)
	{
		formattedName = formattedName.substr(relativePathLocation+1);
	}

	int extensionLocation = formattedName.find_last_of('.');
	if (extensionLocation != std::string::npos)
	{
		formattedName = formattedName.substr(0, extensionLocation) + LevelSelector::textureExt;
	}

	return std::string(LevelSelector::textureAssetPath + formattedName);
}

// Error Functions

void LevelSelector::Parser::HandleError()
{
	fileHandler.close();
}

int LevelSelector::Parser::ErrFindingModelFile(std::string& filePath)
{
	HandleError();
	std::cerr << "Level Parser - ERROR: Could not open file: " << filePath << "\n";
	return LevelSelector::ERR_OPENING_FILE;
}

int LevelSelector::Parser::ErrMalformedFile()
{
	HandleError();
	std::cerr << "Level Parser - ERROR: GameLevel file was malformed.\n";
	return LevelSelector::ERR_MALFORMED_FILE;
}

int LevelSelector::Parser::ErrOpeningFile()
{
	HandleError();
	std::cerr << "Level Parser - ERROR: Failed to open file.\n";
	return LevelSelector::ERR_OPENING_FILE;
}

// Manager Functions

void LevelSelector::Parser::Clear()
{
	levelInfo.totalMaterialCount = 0;
	levelInfo.totalDiffuseCount = 0;
	levelInfo.totalSpecularCount = 0;
	levelInfo.totalNormalCount = 0;

	models.clear();
	cameras.clear();
	lights.clear();
}
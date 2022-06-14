#ifndef _H2BPARSER_H_
#define _H2BPARSER_H_
#include <fstream>
#include <vector>
#include <set>
#include "GraphicsObjects.h"

namespace H2B {
	class Parser
	{
		std::set<std::string> file_strings;
	public:
		char version[4];
		graphics::MODEL model;
		bool Parse(const char* h2bPath)
		{
			Clear();
			std::ifstream file;
			char buffer[260] = { 0, };
			file.open(h2bPath,	std::ios_base::in | 
								std::ios_base::binary);
			if (file.is_open() == false)
				return false;
			file.read(version, 4);
			if (version[1] < '1' || version[2] < '9' || version[3] < 'd')
				return false;
			file.read(reinterpret_cast<char*>(&model.vertexCount), 4);
			file.read(reinterpret_cast<char*>(&model.indexCount), 4);
			file.read(reinterpret_cast<char*>(&model.materialCount), 4);
			file.read(reinterpret_cast<char*>(&model.meshCount), 4);
			model.vertices.resize(model.vertexCount);
			file.read(reinterpret_cast<char*>(model.vertices.data()), 36 * model.vertexCount);
			model.indices.resize(model.indexCount);
			file.read(reinterpret_cast<char*>(model.indices.data()), 4 * model.indexCount);
			model.materials.resize(model.materialCount);
			for (int i = 0; i < model.materialCount; ++i) {
				file.read(reinterpret_cast<char*>(&model.materials[i].attrib), 80);
				for (int j = 0; j < 10; ++j) {
					buffer[0] = '\0';
					*((&model.materials[i].name) + j) = nullptr;
					file.getline(buffer, 260, '\0');
					if (buffer[0] != '\0') {
						auto last = file_strings.insert(buffer);
						*((&model.materials[i].name) + j) = last.first->c_str();
					}
				}
			}
			model.batches.resize(model.materialCount);
			file.read(reinterpret_cast<char*>(model.batches.data()), 8 * model.materialCount);
			model.meshes.resize(model.meshCount);
			for (int i = 0; i < model.meshCount; ++i) {
				buffer[0] = '\0';
				model.meshes[i].name = nullptr;
				file.getline(buffer, 260, '\0');
				if (buffer[0] != '\0') {
					auto last = file_strings.insert(buffer);
					model.meshes[i].name = last.first->c_str();
				}
				file.read(reinterpret_cast<char*>(&model.meshes[i].drawInfo), 8);
				file.read(reinterpret_cast<char*>(&model.meshes[i].materialIndex), 4);
			}
			return true;
		}
		void Clear()
		{
			*reinterpret_cast<unsigned*>(version) = 0;
			file_strings.clear();
			model.clear();
		}
	};
}
#endif
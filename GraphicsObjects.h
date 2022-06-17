#ifndef _LEVELRENDERERGO_H_
#define _LEVELRENDERERGO_H_
#include <vector>
#include "../Gateware/Gateware/Gateware.h"

#pragma pack(push,1)
namespace graphics {
	// Constants
#define REND_DEFAULT_CAMERA { { 0.75f, 0.25f, -1.5f, 1.0f }, { 0.15f, 0.75f, 0.0f, 1.0f }, G_DEGREE_TO_RADIAN(65), 0.1f, 100 }
#define REND_DEFAULT_LIGHT { {-1.0f, -1.0f, 2.0f, 1.0f}, { 0.6f, 0.9f, 1.0f, 1.0f } }

	struct VECTOR {
		float x, y, z;
	};
	struct VERTEX {
		VECTOR pos, uvw, nrm;
	};
	struct alignas(void*) ATTRIBUTES {
		VECTOR Kd; float d;
		VECTOR Ks; float Ns;
		VECTOR Ka; float sharpness;
		VECTOR Tf; float Ni;
		VECTOR Ke; unsigned illum;
	};
	struct BATCH {
		unsigned indexCount, indexOffset;
	};
#pragma pack(pop)
	struct MATERIAL {
		ATTRIBUTES attrib;
		const char* name;
		const char* map_Kd;
		const char* map_Ks;
		const char* map_Ka;
		const char* map_Ke;
		const char* map_Ns;
		const char* map_d;
		const char* disp;
		const char* decal;
		const char* bump;
		const void* padding[2];
	};
	struct MESH {
		const char* name;
		BATCH drawInfo;
		unsigned materialIndex;
	};

	struct MODEL {
		std::string modelName;
		unsigned vertexCount;
		unsigned indexCount;
		unsigned materialCount;
		unsigned meshCount;
		unsigned instanceCount;
		std::vector<graphics::VERTEX> vertices;
		std::vector<unsigned> indices;
		std::vector<graphics::MATERIAL> materials;
		std::vector<graphics::BATCH> batches;
		std::vector<graphics::MESH> meshes;
		std::vector<GW::MATH::GMATRIXF> worldMatrices;

		void clear()
		{
			vertices.clear();
			indices.clear();
			materials.clear();
			batches.clear();
			meshes.clear();
		}
	};

	struct LIGHT
	{
		GW::MATH::GVECTORF Direction;
		GW::MATH::GVECTORF Color;
	};
	struct CAMERA
	{
		GW::MATH::GMATRIXF worldMatrix;
		float FOV;
		float nearPlane;
		float farPlane;
		float aspectRatio;
	};
}

#endif
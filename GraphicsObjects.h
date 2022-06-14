#ifndef _LEVELRENDERERGO_H_
#define _LEVELRENDERERGO_H_
#include <vector>

#pragma pack(push,1)
namespace graphics {
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
		unsigned vertexCount;
		unsigned indexCount;
		unsigned materialCount;
		unsigned meshCount;
		std::vector<graphics::VERTEX> vertices;
		std::vector<unsigned> indices;
		std::vector<graphics::MATERIAL> materials;
		std::vector<graphics::BATCH> batches;
		std::vector<graphics::MESH> meshes;

		void clear()
		{
			vertices.clear();
			indices.clear();
			materials.clear();
			batches.clear();
			meshes.clear();
		}
	};
}

#endif
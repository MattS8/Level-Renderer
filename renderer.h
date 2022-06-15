#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#include <cmath>
#include "GraphicsObjects.h"
#ifdef _WIN32 // must use MT platform DLL libraries on windows
	#pragma comment(lib, "shaderc_combined.lib") 
#endif

/**********************************/
/*  Shader Loader (Shader2String) */
/**********************************/
const char* PIXEL_SHADER_PATH = "Shaders/PixelShader.hlsl";
const char* VERTEX_SHADER_PATH = "Shaders/VertexShader.hlsl";
std::string ShaderAsString(const char* shaderFilePath) {
	std::string output;
	unsigned int stringLength = 0;
	GW::SYSTEM::GFile file; file.Create();
	file.GetFileSize(shaderFilePath, stringLength);
	if (stringLength && +file.OpenBinaryRead(shaderFilePath)) {
		output.resize(stringLength);
		file.Read(&output[0], stringLength);
	}
	else
		std::cout << "ERROR: Shader Source File \"" << shaderFilePath << "\" Not Found!" << std::endl;
	return output;
}

// Simple Vertex Shader
const char* vertexShaderSource = R"(

#pragma pack_matrix(row_major)
// an ultra simple hlsl vertex shader
#define MAX_INSTANCE_PER_DRAW 1024
struct OBJ_ATTRIBUTES
{
	float3	    Kd; // diffuse reflectivity
	float	    d; // dissolve (transparency) 
	float3	    Ks; // specular reflectivity
	float       Ns; // specular exponent
	float3	    Ka; // ambient reflectivity
	float       sharpness; // local reflection map sharpness
	float3	    Tf; // transmission filter
	float       Ni; // optical density (index of refraction)
	float3	    Ke; // emissive reflectivity
	int		    illum; // illumination model
};

struct SHADER_MODEL_DATA
{
	float4 lightDirection;
	float4 lightColor;
	float4 ambientColor;
	float4 cameraPos;
	matrix viewMatrix;
	matrix projectionMatrix;
	matrix matrices[MAX_INSTANCE_PER_DRAW];
	OBJ_ATTRIBUTES materials[MAX_INSTANCE_PER_DRAW];
};

StructuredBuffer<SHADER_MODEL_DATA> SceneData;

[[vk::push_constant]]
cbuffer MESH_INDEX {
	uint mesh_ID;
};

struct VSInput
{
	float3 Position : POSITION;
	float3 UVW : UVW;
	float3 Normal : NORMAL;
}; 

struct VS_OUTPUT
{
	float4 posH : SV_POSITION;
	float3 nrmW : NORMAL;
	float3 posW : WORLD;
	float3 uvw	: UVW;
};


VS_OUTPUT main(VSInput inputVertex, uint InstanceID : SV_InstanceID) : SV_TARGET
{
	VS_OUTPUT vsOut = (VS_OUTPUT)0;
	vsOut.posW = mul(inputVertex.Position, SceneData[0].matrices[InstanceID]);
	vsOut.posH = mul(mul(mul(float4(inputVertex.Position, 1), SceneData[0].matrices[mesh_ID]), SceneData[0].viewMatrix), SceneData[0].projectionMatrix);
	vsOut.nrmW = mul(inputVertex.Normal, SceneData[0].matrices[InstanceID]);
	vsOut.uvw = inputVertex.UVW;
	return vsOut;
}
)";
// Simple Pixel Shader
const char* pixelShaderSource = R"(
#define MAX_INSTANCE_PER_DRAW 1024
struct OBJ_ATTRIBUTES
{
	float3	    Kd; // diffuse reflectivity
	float	    d; // dissolve (transparency) 
	float3	    Ks; // specular reflectivity
	float       Ns; // specular exponent
	float3	    Ka; // ambient reflectivity
	float       sharpness; // local reflection map sharpness
	float3	    Tf; // transmission filter
	float       Ni; // optical density (index of refraction)
	float3	    Ke; // emissive reflectivity
	int		    illum; // illumination model
};

struct SHADER_MODEL_DATA
{
	float4 lightDirection;
	float4 lightColor;
	float4 ambientColor;
	float4 cameraPos;
	matrix viewMatrix;
	matrix projectionMatrix;
	matrix matrices[MAX_INSTANCE_PER_DRAW];
	OBJ_ATTRIBUTES materials[MAX_INSTANCE_PER_DRAW];
};

[[vk::push_constant]]
cbuffer MESH_INDEX {
	uint mesh_ID;
};
// an ultra simple hlsl pixel shader
struct PS_INPUT
{
	float4 posH : SV_POSITION;
	float3 nrmW : NORMAL;
	float3 posW : WORLD;
	float3 uvw	: UVW;
};

StructuredBuffer<SHADER_MODEL_DATA> SceneData;

float4 main(PS_INPUT psInput) : SV_TARGET 
{	
	//float lightRatio = saturate(dot(-normalize(SceneData[0].lightDirection), psInput.nrmW)));
	//float3 lightColor;
	//	lightColor[0] = SceneData[0].ambientColor[0] + lightRatio;
	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
	//float3 resultColor = mul(saturate(lightColor), SceneData[0].materials[mesh_ID].Kd);

	
	// Directional Lighting
	float lightAmount = saturate(dot(-normalize(SceneData[0].lightDirection), normalize(psInput.nrmW)));
	
	// Ambient Lighting
	//float fullAmount = lightAmount;
	//	fullAmount += SceneData[0].ambientColor.x;
	//	fullAmount += SceneData[0].ambientColor.y;
	//	fullAmount += SceneData[0].ambientColor.z;
	//	fullAmount += SceneData[0].ambientColor.w;

	//float fullAmount = saturate(lightAmount + SceneData[0].ambientColor);

	float3 fullAmount;
		fullAmount.x = SceneData[0].ambientColor.x + lightAmount;
		fullAmount.y = SceneData[0].ambientColor.y + lightAmount;
		fullAmount.z = SceneData[0].ambientColor.z + lightAmount;
	fullAmount = saturate(fullAmount);

	//float3 litColor = mul(SceneData[0].materials[mesh_ID].Kd, fullAmount);
	
	float3 litColor = SceneData[0].materials[mesh_ID].Kd;
		litColor.x *= fullAmount.x;
		litColor.y *= fullAmount.y;
		litColor.z *= fullAmount.z;

	float3 worldPos = psInput.posW;
	float3 viewDirection = normalize(SceneData[0].cameraPos - worldPos);
	float3 halfVec = normalize(-normalize(SceneData[0].lightDirection) + viewDirection);
	float intensity = max(pow(saturate(dot(normalize(psInput.nrmW), halfVec)), SceneData[0].materials[mesh_ID].Ns), 0);

	float3 ambientColor = SceneData[0].ambientColor;
	float3 reflectedLight = SceneData[0].lightColor * SceneData[0].materials[mesh_ID].Ks * intensity;
	
	float3 totalLight = litColor + reflectedLight + SceneData[0].materials[mesh_ID].Ke;

	return float4(totalLight, 1);
}
)";
// Creation, Rendering & Cleanup
class Renderer
{
	// Public Structures
public:
	struct Light
	{
		GW::MATH::GVECTORF Direction;
		GW::MATH::GVECTORF Color;
	};
	struct Camera
	{
		GW::MATH::GVECTORF offset;
		GW::MATH::GVECTORF lookAt;
		float FOV;
		float nearPlane;
		float farPlane;
	};

	// Private Structures
private:
	struct GlobalMatrices
	{
		GW::MATH::GMATRIXF world;
		GW::MATH::GMATRIXF view;
		GW::MATH::GMATRIXF projection;
	};

	struct vkObject
	{
		VkBuffer vertexHandle;
		VkDeviceMemory vertexData;
		VkBuffer indexHandle;
		VkDeviceMemory indexData;
	};
	
#define MAX_SUBMESH_PER_DRAW 1024
	struct SHADER_MODEL_DATA
	{
		// globally shared model data
		GW::MATH::GVECTORF lightDirection, lightColor; // Light
		GW::MATH::GVECTORF ambientColor;
		GW::MATH::GVECTORF cameraPos;
		GW::MATH::GMATRIXF viewMatrix, projectionMatrix;
		// per sub-mesh transform and material data
		GW::MATH::GMATRIXF matrices[MAX_SUBMESH_PER_DRAW]; // world space transforms
		graphics::ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW]; // color & texture of surface
	};

	// Constants
	#define REND_DEFAULT_CAMERA { { 0.75f, 0.25f, -1.5f, 1.0f }, { 0.15f, 0.75f, 0.0f, 1.0f }, G_DEGREE_TO_RADIAN(65), 0.1f, 100 }
	#define REND_DEFAULT_LIGHT { {-1.0f, -1.0f, 2.0f, 1.0f}, { 0.6f, 0.9f, 1.0f, 1.0f } }

	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;
	
	// what we need at a minimum to draw a triangle
	std::vector<vkObject> vkObjects;
	VkDevice device = nullptr;

	// Storage Buffers
	std::vector<VkBuffer> storageBuffers;
	std::vector<VkDeviceMemory> storageData;

	VkShaderModule vertexShader = nullptr;
	VkShaderModule pixelShader = nullptr;
	// pipeline settings for drawing (also required)
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	// Descriptor Set Layout
	VkDescriptorSetLayout descLayout = nullptr;
	VkDescriptorSetLayoutCreateInfo descLayoutCreateInfo;
	VkDescriptorSetLayoutBinding descLayoutBinding;

	// Descriptor Set and Pool
	VkDescriptorPool descPool;
	std::vector<VkDescriptorSet> descSets;
	VkDescriptorPoolCreateInfo descPoolCreateInfo;
	VkDescriptorPoolSize descPoolSize;

	// DescriptorSetAllocateInfo
	VkDescriptorSetAllocateInfo descSetAllocateInfo;

	Camera gCamera;
	GlobalMatrices gMatrices;
	Light gLight;

	std::vector<graphics::MODEL> gObjects;

	// Shader Model Data sent to GPU
	SHADER_MODEL_DATA gShaderModelData;
public:

	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk, 
		std::vector<graphics::MODEL> _objects, 
		Camera _camera = REND_DEFAULT_CAMERA,
		Light _light = REND_DEFAULT_LIGHT) 
			: win(_win), vlk(_vlk), gObjects(_objects), gCamera(_camera), gLight(_light)
	{
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);
		// Set Up Matrices
		{			
			GW::MATH::GMatrix::IdentityF(gMatrices.world);
			GW::MATH::GMatrix::IdentityF(gMatrices.view);

			float aspectRatio;
			vlk.GetAspectRatio(aspectRatio);
			GW::MATH::GMatrix::ProjectionDirectXLHF(gCamera.FOV, aspectRatio, gCamera.nearPlane, gCamera.farPlane, gMatrices.projection);

			GW::MATH::GVECTORF upVector;
			upVector.x = 0;
			upVector.y = 1;
			upVector.z = 0;
			upVector.w = 1;
			GW::MATH::GMatrix::LookAtLHF(gCamera.offset, gCamera.lookAt, upVector, gMatrices.view);
		}

		// Set Shader Model Data
		{
			gShaderModelData.lightColor = gLight.Color;
			gShaderModelData.lightDirection = gLight.Direction;
			gShaderModelData.viewMatrix = gMatrices.view;
			gShaderModelData.projectionMatrix = gMatrices.projection;

			// TODO set up on per-object basis
			gShaderModelData.ambientColor.x = 0.25f;
			gShaderModelData.ambientColor.y = 0.25f;
			gShaderModelData.ambientColor.z = 0.35f;
			gShaderModelData.ambientColor.w = 1;
			gShaderModelData.cameraPos.x = gCamera.offset.x;
			gShaderModelData.cameraPos.y = gCamera.offset.y;
			gShaderModelData.cameraPos.z = gCamera.offset.z;
			gShaderModelData.cameraPos.w = gCamera.offset.w;
			//GW::MATH::GMatrix::InverseF(gMatrices.view, gShaderModelData.cameraPos);
			gShaderModelData.matrices[0] = gMatrices.world;
			gShaderModelData.matrices[1] = gMatrices.world;
			gShaderModelData.materials[0] = gObjects[0].materials[0].attrib;
			gShaderModelData.materials[1] = gObjects[0].materials[1].attrib;
		}

		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		VkPhysicalDevice physicalDevice = nullptr;
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);

		
		unsigned int totalNumVerts = 0;
		vkObjects.resize(gObjects.size());
		for (int i = 0; i < gObjects.size(); i++)
		{
			// Create Vertex Buffer
			unsigned int numBytes = sizeof(graphics::VERTEX) * gObjects[i].vertexCount;
			GvkHelper::create_buffer(physicalDevice, device, numBytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &(vkObjects[i].vertexHandle), &(vkObjects[i].vertexData));
			GvkHelper::write_to_buffer(device, vkObjects[i].vertexData, &(gObjects[i].vertices.front()), numBytes);

			// Create Index Buffer
			numBytes = sizeof(unsigned int) * gObjects[i].indexCount;
			GvkHelper::create_buffer(physicalDevice, device, numBytes,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &(vkObjects[i].indexHandle), &(vkObjects[i].indexData));
			GvkHelper::write_to_buffer(device, vkObjects[i].indexData, &(gObjects[i].indices.front()), numBytes);
		}

		unsigned int chainSwapCount;
		vlk.GetSwapchainImageCount(chainSwapCount);
		storageBuffers.resize(chainSwapCount);
		storageData.resize(chainSwapCount);
		for (unsigned int i = 0; i < chainSwapCount; i++)
		{
			GvkHelper::create_buffer(physicalDevice, device, sizeof(SHADER_MODEL_DATA),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storageBuffers[i], &storageData[i]);
			GvkHelper::write_to_buffer(device, storageData[i], &gShaderModelData, sizeof(SHADER_MODEL_DATA));
		}

		/***************** SHADER INTIALIZATION ******************/
		// Initialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(options, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(options, true);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(options);
#endif
		// Create Vertex Shader
		const char* vertextShaderStr = ShaderAsString(VERTEX_SHADER_PATH).c_str();
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource, strlen(vertexShaderSource),
			shaderc_vertex_shader, "main.vert", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Vertex Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);
		shaderc_result_release(result); // done
		// Create Pixel Shader
		const char* pixelShaderStr = ShaderAsString(PIXEL_SHADER_PATH).c_str();
		result = shaderc_compile_into_spv( // compile
			compiler, pixelShaderSource, strlen(pixelShaderSource),
			shaderc_fragment_shader, "main.frag", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Pixel Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &pixelShader);
		shaderc_result_release(result); // done
		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);

		/***************** PIPELINE INTIALIZATION ******************/
		// Create Pipeline & Layout (Thanks Tiny!)
		VkRenderPass renderPass;
		vlk.GetRenderPass((void**)&renderPass);
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};
		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";
		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = pixelShader;
		stage_create_info[1].pName = "main";
		// Assembly State
		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = {};
		assembly_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assembly_create_info.primitiveRestartEnable = false;
		// Vertex Input State
		VkVertexInputBindingDescription vertex_binding_description = {};
		vertex_binding_description.binding = 0;
		vertex_binding_description.stride = sizeof(graphics::VERTEX);
		vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		VkVertexInputAttributeDescription vertex_attribute_description[3] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(graphics::VERTEX, uvw)},
			{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(graphics::VERTEX, nrm) }
			//uv, normal, etc....
		};
		VkPipelineVertexInputStateCreateInfo input_vertex_info = {};
		input_vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		input_vertex_info.vertexBindingDescriptionCount = 1;
		input_vertex_info.pVertexBindingDescriptions = &vertex_binding_description;
		input_vertex_info.vertexAttributeDescriptionCount = 3;
		input_vertex_info.pVertexAttributeDescriptions = vertex_attribute_description;
		// Viewport State (we still need to set this up even though we will overwrite the values)
		VkViewport viewport = {
            0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
        };
        VkRect2D scissor = { {0, 0}, {width, height} };
		VkPipelineViewportStateCreateInfo viewport_create_info = {};
		viewport_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_create_info.viewportCount = 1;
		viewport_create_info.pViewports = &viewport;
		viewport_create_info.scissorCount = 1;
		viewport_create_info.pScissors = &scissor;
		// Rasterizer State
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = {};
		rasterization_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization_create_info.rasterizerDiscardEnable = VK_FALSE;
		rasterization_create_info.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization_create_info.lineWidth = 1.0f;
		rasterization_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterization_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterization_create_info.depthClampEnable = VK_FALSE;
		rasterization_create_info.depthBiasEnable = VK_FALSE;
		rasterization_create_info.depthBiasClamp = 0.0f;
		rasterization_create_info.depthBiasConstantFactor = 0.0f;
		rasterization_create_info.depthBiasSlopeFactor = 0.0f;
		// Multisampling State
		VkPipelineMultisampleStateCreateInfo multisample_create_info = {};
		multisample_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample_create_info.sampleShadingEnable = VK_FALSE;
		multisample_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample_create_info.minSampleShading = 1.0f;
		multisample_create_info.pSampleMask = VK_NULL_HANDLE;
		multisample_create_info.alphaToCoverageEnable = VK_FALSE;
		multisample_create_info.alphaToOneEnable = VK_FALSE;
		// Depth-Stencil State
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {};
		depth_stencil_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil_create_info.depthTestEnable = VK_TRUE;
		depth_stencil_create_info.depthWriteEnable = VK_TRUE;
		depth_stencil_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
		depth_stencil_create_info.minDepthBounds = 0.0f;
		depth_stencil_create_info.maxDepthBounds = 1.0f;
		depth_stencil_create_info.stencilTestEnable = VK_FALSE;
		// Color Blending Attachment & State
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
		color_blend_attachment_state.colorWriteMask = 0xF;
		color_blend_attachment_state.blendEnable = VK_FALSE;
		color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = {};
		color_blend_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend_create_info.logicOpEnable = VK_FALSE;
		color_blend_create_info.logicOp = VK_LOGIC_OP_COPY;
		color_blend_create_info.attachmentCount = 1;
		color_blend_create_info.pAttachments = &color_blend_attachment_state;
		color_blend_create_info.blendConstants[0] = 0.0f;
		color_blend_create_info.blendConstants[1] = 0.0f;
		color_blend_create_info.blendConstants[2] = 0.0f;
		color_blend_create_info.blendConstants[3] = 0.0f;
		// Dynamic State 
		VkDynamicState dynamic_state[2] = { 
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamic_create_info = {};
		dynamic_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_create_info.dynamicStateCount = 2;
		dynamic_create_info.pDynamicStates = dynamic_state;

		{
			descLayoutBinding = {};
			descLayoutBinding.binding = 0;
			descLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			descLayoutBinding.descriptorCount = 1;
			descLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			descLayoutBinding.pImmutableSamplers = nullptr;

			descLayoutCreateInfo = {};
			descLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descLayoutCreateInfo.bindingCount = 1;
			descLayoutCreateInfo.pBindings = &descLayoutBinding;
			descLayoutCreateInfo.pNext = nullptr;
			descLayoutCreateInfo.flags = 0;

			vkCreateDescriptorSetLayout(device, &descLayoutCreateInfo, nullptr, &descLayout);
		}

		{
			descPoolSize = {};
			descPoolSize.descriptorCount = chainSwapCount;
			descPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

			descPoolCreateInfo = {};
			descPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descPoolCreateInfo.pNext = nullptr;
			descPoolCreateInfo.flags = 0;
			descPoolCreateInfo.maxSets = chainSwapCount;
			descPoolCreateInfo.poolSizeCount = 1;
			descPoolCreateInfo.pPoolSizes = &descPoolSize;

			vkCreateDescriptorPool(device, &descPoolCreateInfo, nullptr, &descPool);
		}

		{
			descSets.resize(chainSwapCount);
			descSetAllocateInfo = {};
			descSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descSetAllocateInfo.pNext = nullptr;
			descSetAllocateInfo.descriptorPool = descPool;
			descSetAllocateInfo.descriptorSetCount = 1;
			descSetAllocateInfo.pSetLayouts = &descLayout;

			for (int i = 0; i < chainSwapCount; i++)
				vkAllocateDescriptorSets(device, &descSetAllocateInfo, &descSets[i]);
		}

		{
			for (int i = 0; i < chainSwapCount; i++)
			{
				VkDescriptorBufferInfo descBufferInfo = {};
				descBufferInfo.buffer = storageBuffers[i];
				descBufferInfo.offset = 0;
				descBufferInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet writeDescSet = {};
				writeDescSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescSet.pNext = nullptr;
				writeDescSet.dstSet = descSets[i];
				writeDescSet.dstBinding = 0;
				writeDescSet.dstArrayElement = 0;
				writeDescSet.descriptorCount = 1;
				writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				writeDescSet.pImageInfo = nullptr;
				writeDescSet.pBufferInfo = &descBufferInfo;
				writeDescSet.pTexelBufferView = nullptr;

				vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);
			}
		}
	
		// Descriptor pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		{
			pipeline_layout_create_info.setLayoutCount = 1;
			pipeline_layout_create_info.pSetLayouts = &descLayout;
		}

		VkPushConstantRange constantRange;
		constantRange = {};
		constantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		constantRange.offset = 0;
		constantRange.size = sizeof(unsigned int);
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &constantRange;
		vkCreatePipelineLayout(device, &pipeline_layout_create_info, 
			nullptr, &pipelineLayout);
	    // Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, 
			&pipeline_create_info, nullptr, &pipeline);

		/***************** CLEANUP / SHUTDOWN ******************/
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
		});
	}

	void RotateLogo()
	{
		static std::chrono::steady_clock::time_point timePoint = std::chrono::high_resolution_clock::now();
		auto currentTime = std::chrono::high_resolution_clock::now();
		float timePassed = std::chrono::duration<float, std::milli>(currentTime - timePoint).count() / 1000; // Time in seconds
		static float totalRotation = 0;
		static float totalTime = 0;
		totalTime += timePassed;
		
		//float rotationAmount = G_DEGREE_TO_RADIAN(10) * timePassed * (std::sinf(totalTime) * 2);
		//rotationAmount += G_DEGREE_TO_RADIAN(10) * (std::sinf(totalRotation) + 2) * timePassed;
		float rotationAmount = G_DEGREE_TO_RADIAN(25) * timePassed;
		totalRotation += rotationAmount;
		GW::MATH::GMatrix::RotateYGlobalF(gShaderModelData.matrices[1], rotationAmount, gShaderModelData.matrices[1]);
		
		timePoint = std::chrono::high_resolution_clock::now();
	}

	void Render()
	{
		// grab the current Vulkan commandBuffer
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);
		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		// what is the current client area dimensions?
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);
		// setup the pipeline's dynamic settings
		VkViewport viewport = {
            0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
        };
        VkRect2D scissor = { {0, 0}, {width, height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		
		// now we can draw
		unsigned int currentImageIndex;
		vlk.GetSwapchainCurrentImage(currentImageIndex);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &descSets[currentImageIndex], 0, nullptr);

		for (int i = 0; i < gObjects.size(); i++)
		{
			graphics::MODEL obj = gObjects[i];
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vkObjects[i].vertexHandle), offsets);
			vkCmdBindIndexBuffer(commandBuffer, vkObjects[i].indexHandle, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
			memcpy(gShaderModelData.materials, &(obj.materials.front()), sizeof(graphics::MATERIAL) * obj.materialCount);
			memcpy(gShaderModelData.matrices, &(obj.worldMatrices.front()), sizeof(GW::MATH::GMATRIXF) * obj.instanceCount);
			GvkHelper::write_to_buffer(device, storageData[currentImageIndex], &gShaderModelData, sizeof(SHADER_MODEL_DATA));
			
			for (int j = 0; j < obj.meshCount; j++)
			{
				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(unsigned int), &j);
				vkCmdDrawIndexed(commandBuffer, obj.meshes[j].drawInfo.indexCount, obj.instanceCount, 
					obj.meshes[j].drawInfo.indexOffset, 0, 0);
			}
		}
		
		//vkCmdDrawIndexed(commandBuffer, 5988)
		//vkCmdDraw(commandBuffer, 3885, 1, 0, 0);
		//vkCmdDrawIndexed(commandBuffer, 8532, 1, 0, 0, 1);
		
	}
	
private:
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);
		// Release allocated buffers, shaders & pipeline
		for (vkObject vkObj : vkObjects)
		{
			vkDestroyBuffer(device, vkObj.indexHandle, nullptr);
			vkFreeMemory(device, vkObj.indexData, nullptr);
			vkDestroyBuffer(device, vkObj.vertexHandle, nullptr);
			vkFreeMemory(device, vkObj.vertexData, nullptr);
		}
		
		for (VkBuffer& buffer : storageBuffers)
			vkDestroyBuffer(device, buffer, nullptr);
		for (VkDeviceMemory& data : storageData)
			vkFreeMemory(device, data, nullptr);

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, pixelShader, nullptr);
		vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
		vkDestroyDescriptorPool(device, descPool, nullptr);
		
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};

#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#include <cmath>
#include "GraphicsObjects.h"
#include "LevelSelector.h"
#define KHRONOS_STATIC 
#include "ktx.h"
#include <ktxvulkan.h>

#ifdef _WIN32 // must use MT platform DLL libraries on windows
	#pragma comment(lib, "shaderc_combined.lib") 
#endif

/**********************************/
/*  Shader Loader (Shader2String) */
/**********************************/
const char* PIXEL_SHADER_PATH = "../Shaders/PixelShader.hlsl";
const char* VERTEX_SHADER_PATH = "../Shaders/VertexShader.hlsl";
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

// Creation, Rendering & Cleanup
class Renderer
{
private:
	struct PushConstants
	{
		unsigned int material_offset;
		unsigned int matrix_offset;
	};

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
	struct VERTEX_SHADER_DATA
	{
		GW::MATH::GMATRIXF viewMatrix, projectionMatrix;
		GW::MATH::GVECTORF cameraPos;
		GW::MATH::GMATRIXF matrices[MAX_SUBMESH_PER_DRAW]; // world space transforms
	};

	struct PIXEL_SHADER_DATA
	{
		GW::MATH::GVECTORF lightDirection;
		GW::MATH::GVECTORF lightColor; 
		GW::MATH::GVECTORF ambientColor;
	};

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

	// Defaults
	graphics::CAMERA DefaultCamera;
	//#define REND_DEFAULT_CAMERA { { 0.75f, 0.25f, -1.5f, 1.0f }, { 0.15f, 0.75f, 0.0f, 1.0f }, G_DEGREE_TO_RADIAN(65), 0.1f, 100 }
	#define REND_DEFAULT_LIGHT { {-1.0f, -1.0f, 2.0f, 1.0f}, { 0.6f, 0.9f, 1.0f, 1.0f } }

	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;
	
	// what we need at a minimum to draw a triangle
	std::vector<vkObject> vkObjects;
	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;

	// Matrix Storage Buffers
	std::vector<VkBuffer> gMatrixBuffers;
	std::vector<VkDeviceMemory> gMatrixData;
	std::vector<VkDescriptorSet> gMatrixDescriptorSets;
	VkDescriptorSetLayout gVertexDescriptorLayout = nullptr;

	/***************** KTX+VULKAN TEXTURING VARIABLES ******************/
	#define DEFAULT_DIFFUSE_MAP  "../DefaultTextures/defaultDiffuse.ktx"
	#define DEFAULT_SPECULAR_MAP "../DefaultTextures/defaultSpecular.ktx"
	#define DEFAULT_NORMAL_MAP "../DefaultTextures/defaultNormal.ktx"

	std::vector<ktxVulkanTexture> gDiffuseTextures; // one per texture
	std::vector<VkImageView> gDiffuseTextureViews; // one per texture
	std::vector<ktxVulkanTexture> gSpecularTextures; // one per texture
	std::vector<VkImageView> gSpecularTextureViews; // one per texture

	VkSampler gTextureSampler = nullptr; // can be shared, effects quality & addressing mode

	// note that unlike uniform buffers, we don't need one for each "in-flight" frame
	std::vector<VkDescriptorSet> gDiffuseTextureDescriptorSets;
	std::vector<VkDescriptorSet> gSpecularTextureDescriptorSets;

	// be aware that all pipeline shaders share the same bind points
	//VkDescriptorSetLayout gPixelDescriptorLayout = nullptr;

	// textures can optionally share descriptor sets/pools/layouts with uniform & storage buffers	
	VkDescriptorPool gDescriptorPool = nullptr;

	/***************** ****************************** ******************/

	VkShaderModule vertexShader = nullptr;
	VkShaderModule pixelShader = nullptr;
	// pipeline settings for drawing (also required)
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	// Descriptor Set Layout
	VkDescriptorSetLayout descriptorSetLayout_Vertex = nullptr;
	VkDescriptorSetLayout descriptorSetLayout_Pixel = nullptr;
	VkDescriptorSetLayoutCreateInfo descLayoutCreateInfo;
	VkDescriptorSetLayoutBinding descriptorLayoutBinding_Vertex;
	VkDescriptorSetLayoutBinding descriptorLayoutBinding_Pixel;

	// Descriptor Set and Pool
	VkDescriptorPool descPool;
	VkDescriptorPoolCreateInfo descPoolCreateInfo;
	VkDescriptorPoolSize descPoolSize;

	// DescriptorSetAllocateInfo
	VkDescriptorSetAllocateInfo descSetAllocateInfo;

	std::vector<graphics::CAMERA> gCameras;
	graphics::CAMERA gCamera;
	GlobalMatrices gMatrices;
	Light gLight;

	std::vector<graphics::MODEL> gObjects;
	LevelSelector::Selector gLevelSelector;

	// Shader Model Data sent to GPU
	SHADER_MODEL_DATA gShaderModelData;
	//VERTEX_SHADER_DATA gVertexShaderData;

	// Input Controls
	GW::INPUT::GInput gInputProxy;
	GW::INPUT::GController gControllerProxy;
	GW::INPUT::GBufferedInput gBufferedInputProxy;
public:
	struct InputModifiers
	{
		float CameraSpeed;
		float LookSensitivity;
	};
	InputModifiers inputModifiers;
	float maxCameraSpeed, minCameraSpeed;
	float maxSensitivity, minSensitivity;

	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk,
		Light _light = REND_DEFAULT_LIGHT) 
			: win(_win), vlk(_vlk), gLight(_light)
	{
		VkResult res;
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);

		// Setup input controllers
		gInputProxy.Create(win);
		gControllerProxy.Create();
		gBufferedInputProxy.Create(win);

		// Select initial level
		gLevelSelector.SelectNewLevel(true);
		gLevelSelector.ParseSelectedLevel();

		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		
		ChangeLevel(gLevelSelector.levelParser.ModelsToVector(), gLevelSelector.levelParser.CamerasToVector());

		unsigned int chainSwapCount;
		vlk.GetSwapchainImageCount(chainSwapCount);
		gMatrixBuffers.resize(chainSwapCount);
		gMatrixData.resize(chainSwapCount);
		for (unsigned int i = 0; i < chainSwapCount; i++)
		{
			GvkHelper::create_buffer(physicalDevice, device, sizeof(SHADER_MODEL_DATA),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &gMatrixBuffers[i], &gMatrixData[i]);
		}
		WriteModelsToShaderData();
		gDiffuseTextures.resize(gLevelSelector.levelParser.levelInfo.totalDiffuseCount);
		gDiffuseTextureViews.resize(gLevelSelector.levelParser.levelInfo.totalDiffuseCount);

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
		std::string vertexShaderStr = ShaderAsString(VERTEX_SHADER_PATH);
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderStr.c_str(), vertexShaderStr.length(),
			shaderc_vertex_shader, "main.vert", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Vertex Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);
		shaderc_result_release(result); // done

		// Create Pixel Shader
		std::string pixelShaderStr = ShaderAsString(PIXEL_SHADER_PATH);
		result = shaderc_compile_into_spv( // compile
			compiler, pixelShaderStr.c_str(), pixelShaderStr.length(),
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
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(graphics::VERTEX, uvw) },
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

		// Describes the order and type of resources bound to the vertex shader
		
		descriptorLayoutBinding_Vertex = {};
		descriptorLayoutBinding_Vertex.binding = 0;
		descriptorLayoutBinding_Vertex.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorLayoutBinding_Vertex.descriptorCount = 1;
		descriptorLayoutBinding_Vertex.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		descriptorLayoutBinding_Vertex.pImmutableSamplers = nullptr;

		// Create vertex shader layout
		descLayoutCreateInfo = {};
		descLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descLayoutCreateInfo.bindingCount = 1;
		descLayoutCreateInfo.pBindings = &descriptorLayoutBinding_Vertex;
		descLayoutCreateInfo.pNext = nullptr;
		descLayoutCreateInfo.flags = 0;

		res = vkCreateDescriptorSetLayout(device, &descLayoutCreateInfo, nullptr, &descriptorSetLayout_Vertex);
		if (res != VkResult::VK_SUCCESS)
		{
			std::cerr << "ERROR: Unable to create Vertex Layout Descriptor Set!\n";
			return;
		}

		// Describes the order and type of resources bound to the pixel shader
		descriptorLayoutBinding_Pixel = {};
		descriptorLayoutBinding_Pixel.binding = 0;
		descriptorLayoutBinding_Pixel.descriptorCount = 1;
		descriptorLayoutBinding_Pixel.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorLayoutBinding_Pixel.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		descriptorLayoutBinding_Pixel.pImmutableSamplers = nullptr;

		// Pixel shader will have its own descriptor set layout
		descLayoutCreateInfo.pBindings = &descriptorLayoutBinding_Pixel;
		res = vkCreateDescriptorSetLayout(device, &descLayoutCreateInfo, nullptr, &descriptorSetLayout_Pixel);
		if (res != VkResult::VK_SUCCESS)
		{
			std::cerr << "ERROR: Unable to create Pixel Layout Descriptor Set!\n";
			return;
		}

		// Descriptor Sets for Textures 
		
		// Create a descriptor pool!
		// this is how many unique descriptor sets you want to allocate 
		// we need one for each uniform buffer and one for each unique texture
		unsigned int diffuseDescriptorCount = gLevelSelector.levelParser.levelInfo.totalDiffuseCount + 1;
		unsigned int total_descriptorsets = gMatrixBuffers.size() + diffuseDescriptorCount;
		VkDescriptorPoolSize descriptorPoolSize[2] = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gMatrixBuffers.size() },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, diffuseDescriptorCount}
		};

		descPoolCreateInfo = {};
		descPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descPoolCreateInfo.flags = 0;
		descPoolCreateInfo.maxSets = total_descriptorsets;
		descPoolCreateInfo.poolSizeCount = 2;
		descPoolCreateInfo.pPoolSizes = descriptorPoolSize;
		descPoolCreateInfo.pNext = nullptr;
		res = vkCreateDescriptorPool(device, &descPoolCreateInfo, nullptr, &descPool);
		if (res != VkResult::VK_SUCCESS)
		{
			std::cerr << "ERROR: Unable to create descriptorPool!\n";
			return;
		}

		// Create a descriptor sets for our diffuse textures!
		
		gDiffuseTextureDescriptorSets.resize(diffuseDescriptorCount);
		VkDescriptorSetAllocateInfo descriptorsetAllocateInfo = {};
		descriptorsetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorsetAllocateInfo.descriptorSetCount = 1;
		descriptorsetAllocateInfo.pSetLayouts = &descriptorSetLayout_Pixel;
		descriptorsetAllocateInfo.descriptorPool = descPool;
		descriptorsetAllocateInfo.pNext = nullptr;
		for (int i = 0; i < diffuseDescriptorCount; i++)
		{
			res = vkAllocateDescriptorSets(device, &descriptorsetAllocateInfo, &gDiffuseTextureDescriptorSets[i]);
			if (res != VkResult::VK_SUCCESS)
			{
				std::cerr << "ERROR: Unable to allocate descriptorSets!\n";
				return;
			}
		}

		// Create descriptor sets for matrix Buffers
		descriptorsetAllocateInfo.pSetLayouts = &descriptorSetLayout_Vertex;
		VkWriteDescriptorSet writeDescriptorSet = {};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstArrayElement = 0;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		VkDescriptorBufferInfo descriptorBufferInfo = { nullptr, 0, VK_WHOLE_SIZE };
		writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
		gMatrixDescriptorSets.resize(chainSwapCount);
		for (int i = 0; i < chainSwapCount; i++)
		{
			res = vkAllocateDescriptorSets(device, &descriptorsetAllocateInfo, &gMatrixDescriptorSets[i]);
			if (res != VkResult::VK_SUCCESS)
			{
				std::cerr << "ERROR: Unable to allocate matrix descriptorSets!\n";
				return;
			}
			writeDescriptorSet.dstSet = gMatrixDescriptorSets[i];
			descriptorBufferInfo.buffer = gMatrixBuffers[i];
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}
	
		// Descriptor pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 2;
		VkDescriptorSetLayout layouts[2] = { descriptorSetLayout_Vertex, descriptorSetLayout_Pixel };
		pipeline_layout_create_info.pSetLayouts = layouts;
		
		// Push Constant layout
		VkPushConstantRange constantRange;
		constantRange = {};
		constantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		constantRange.offset = 0;
		constantRange.size = sizeof(PushConstants);
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &constantRange;
		vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipelineLayout);

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
		
		// With pipeline created, lets load in our texture and bind it to our descriptor set
		LoadDiffuseTextures();

		/***************** CLEANUP / SHUTDOWN ******************/
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
		});
	}

	void UpdateCamera()
	{
		static std::chrono::steady_clock::time_point timePoint = std::chrono::high_resolution_clock::now();
		auto currentTime = std::chrono::high_resolution_clock::now();
		float timePassed = std::chrono::duration<float, std::milli>(currentTime - timePoint).count() / 1000; // Time in seconds																								 
		// TODO: Part 4c
		GW::MATH::GMatrix::InverseF(gMatrices.view, gMatrices.view);
		// TODO: Part 4d
		float temp = 0;
		GW::MATH::GVECTORF translationVector;
		bool bControllerConnected;
		gControllerProxy.IsConnected(0, bControllerConnected);

		// Camera Speed
		float scrollChange = 0.0f;



		// Look Sensitivity


		// y-axis Movement
		float yChange = 0;
		gInputProxy.GetState(G_KEY_SPACE, temp);
		yChange += temp;
		gInputProxy.GetState(G_KEY_LEFTSHIFT, temp);
		yChange -= temp;
		if (bControllerConnected)
		{
			gControllerProxy.GetState(0, G_RIGHT_TRIGGER_AXIS, temp);
			yChange += temp;
			gControllerProxy.GetState(0, G_LEFT_TRIGGER_AXIS, temp);
			yChange -= temp;
		}
		yChange = yChange * inputModifiers.CameraSpeed * timePassed;
		translationVector.x = translationVector.z = 0;
		translationVector.w = 1;
		translationVector.y = yChange;
		GW::MATH::GMatrix::TranslateGlobalF(gMatrices.view, translationVector, gMatrices.view);

		// x-axis Movement
		float xChange = 0;
		gInputProxy.GetState(G_KEY_D, temp);
		xChange += temp;
		gInputProxy.GetState(G_KEY_A, temp);
		xChange -= temp;
		if (bControllerConnected)
		{
			gControllerProxy.GetState(0, G_LX_AXIS, temp);
			xChange += temp;
		}
		xChange = xChange * inputModifiers.CameraSpeed * timePassed;

		// z-axis Movement
		float zChange = 0;
		gInputProxy.GetState(G_KEY_W, temp);
		zChange += temp;
		gInputProxy.GetState(G_KEY_S, temp);
		zChange -= temp;
		if (bControllerConnected)
		{
			gControllerProxy.GetState(0, G_LY_AXIS, temp);
			zChange += temp;
		}
		zChange = zChange * inputModifiers.CameraSpeed * timePassed;

		translationVector.x = xChange;
		translationVector.z = zChange;
		translationVector.y = 0;
		translationVector.w = 1;
		GW::MATH::GMatrix::TranslateLocalF(gMatrices.view, translationVector, gMatrices.view);


		// TODO: Part 4e
		float thumbSpeed = inputModifiers.LookSensitivity * timePassed;
		float mouseDelta[2];
		unsigned int windowHeight;
		win.GetHeight(windowHeight);
		bool bRMBClicked = false;
		gInputProxy.GetState(G_BUTTON_RIGHT, temp);
		bRMBClicked = temp > 0;
		bool bUpdatePitchAndYaw = gInputProxy.GetMouseDelta(mouseDelta[0], mouseDelta[1]) != GW::GReturn::REDUNDANT;
		if (bRMBClicked && bUpdatePitchAndYaw)
		{
			float totalPitch = gCamera.FOV * mouseDelta[1] / windowHeight;
			if (bControllerConnected)
			{
				gControllerProxy.GetState(0, G_RY_AXIS, temp);
				totalPitch += temp;
			}
			totalPitch *= thumbSpeed;

			GW::MATH::GMATRIXF pitchMatrix;
			GW::MATH::GMatrix::IdentityF(pitchMatrix);
			GW::MATH::GMatrix::RotateXGlobalF(pitchMatrix, totalPitch, pitchMatrix);
			GW::MATH::GMatrix::MultiplyMatrixF(pitchMatrix, gMatrices.view, gMatrices.view);
		}

		unsigned int windowWidth;
		win.GetWidth(windowWidth);

		if (bRMBClicked && bUpdatePitchAndYaw)
		{
			float totalYaw = gCamera.FOV * gCamera.aspectRatio * mouseDelta[0] / windowWidth;
			if (bControllerConnected)
			{
				gControllerProxy.GetState(0, G_RX_AXIS, temp);
				totalYaw += temp;
			}
			totalYaw *= thumbSpeed;

			GW::MATH::GMATRIXF yawMatrix;
			GW::MATH::GMatrix::IdentityF(yawMatrix);
			GW::MATH::GMatrix::RotateYLocalF(yawMatrix, totalYaw, yawMatrix);
			GW::MATH::GVECTORF originalPos;
			originalPos.x = gMatrices.view.row4.x;
			originalPos.y = gMatrices.view.row4.y;
			originalPos.z = gMatrices.view.row4.z;
			originalPos.w = gMatrices.view.row4.w;
			GW::MATH::GMatrix::MultiplyMatrixF(gMatrices.view, yawMatrix, gMatrices.view);
			gMatrices.view.row4.x = originalPos.x;
			gMatrices.view.row4.y = originalPos.y;
			gMatrices.view.row4.z = originalPos.z;
			gMatrices.view.row4.w = originalPos.w;
		}

		GW::MATH::GMatrix::InverseF(gMatrices.view, gMatrices.view);
		gShaderModelData.viewMatrix = gMatrices.view;
		//gVertexShaderData.viewMatrix = gMatrices.view;

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

		// Update Camera
		vlk.GetAspectRatio(gCamera.aspectRatio);
		GW::MATH::GMatrix::ProjectionDirectXLHF(gCamera.FOV, gCamera.aspectRatio,
			gCamera.nearPlane, gCamera.farPlane, gMatrices.projection);
		gShaderModelData.projectionMatrix = gMatrices.projection;
		//gVertexShaderData.projectionMatrix = gMatrices.projection;
		
		// now we can draw
		unsigned int currentImageIndex;
		vlk.GetSwapchainCurrentImage(currentImageIndex);

		// Bind Matrix Descriptor Sets to Vertex Shader
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &gMatrixDescriptorSets[currentImageIndex], 0, nullptr);
		GvkHelper::write_to_buffer(device, gMatrixData[currentImageIndex], &gShaderModelData, sizeof(SHADER_MODEL_DATA));

		VkDeviceSize offsets[] = { 0 };
		PushConstants pushConstants = { 0, 0};

		unsigned int diffuseOffset = 1;
		unsigned int specularOffset = 1;
		for (int i = 0; i < gObjects.size(); i++)
		{
			graphics::MODEL obj = gObjects[i];
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vkObjects[i].vertexHandle), offsets);
			vkCmdBindIndexBuffer(commandBuffer, vkObjects[i].indexHandle, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
			
			// Reset offset counters
			unsigned int tDiffuseCount = 0;
			unsigned int tSpecularCount = 0;

			// Loop through each submesh and bind textures/offsets
			for (int j = 0; j < obj.meshCount; j++)
			{
				// Bind Diffuse Texture Descriptor Sets to Pixel Shader
				if (obj.materialInfo.diffuseCount > tDiffuseCount)
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						pipelineLayout, 1, 1,
						&(gDiffuseTextureDescriptorSets[diffuseOffset++]), 0, nullptr);
				else
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						pipelineLayout, 1, 1,
						&(gDiffuseTextureDescriptorSets[0]), 0, nullptr);

				// TODO: Bind Specular Texture Descriptor Sets to Pixel Shader


				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(PushConstants), &pushConstants);
				vkCmdDrawIndexed(commandBuffer, obj.meshes[j].drawInfo.indexCount, obj.instanceCount, 
					obj.meshes[j].drawInfo.indexOffset, 0, 0);
				pushConstants.material_offset += 1;
			}

			pushConstants.matrix_offset += obj.materialInfo.materialCount;
		}
	}

	void CheckCommands()
	{
		float keyState;
		gInputProxy.GetState(G_KEY_F1, keyState);
		if (keyState > 0 && !gLevelSelector.IsCurrentlySelectingFile() && gLevelSelector.SelectNewLevel(false))
		{
			CleanUpVertexAndIndexBuffers();
			gLevelSelector.ParseSelectedLevel();
			ChangeLevel(gLevelSelector.levelParser.ModelsToVector(), gLevelSelector.levelParser.CamerasToVector());
			WriteModelsToShaderData();
		}
	}

private:
	void ChangeLevel(std::vector<graphics::MODEL> _objects, std::vector<graphics::CAMERA> _cameras)
	{
		gObjects = _objects;
		gCameras = _cameras;

		maxCameraSpeed = 13.0f;
		minCameraSpeed = 0.1f;
		maxSensitivity = G_PI * 1000 * 10;
		minSensitivity = G_PI * 1000 * 0.5f;
		inputModifiers.CameraSpeed = (maxCameraSpeed + minCameraSpeed) / 2;
		inputModifiers.LookSensitivity = (maxSensitivity + minSensitivity) / 4;

		// Set up main camera
		GW::MATH::GMatrix::IdentityF(DefaultCamera.worldMatrix);
		DefaultCamera.farPlane = 1000.0f;
		DefaultCamera.nearPlane = 0.1f;
		DefaultCamera.FOV = G_DEGREE_TO_RADIAN(90);

		gCamera = gCameras.size() == 0 ? DefaultCamera : gCameras[0];

		// Set Up Matrices
		{
			GW::MATH::GMatrix::IdentityF(gMatrices.world);
			GW::MATH::GMatrix::InverseF(gCamera.worldMatrix, gMatrices.view);

			vlk.GetAspectRatio(gCamera.aspectRatio);
			GW::MATH::GMatrix::ProjectionDirectXLHF(gCamera.FOV, gCamera.aspectRatio,
				gCamera.nearPlane, gCamera.farPlane, gMatrices.projection);
		}

		// Set Shader Model Data
		{
			gShaderModelData.lightColor = gLight.Color;
			gShaderModelData.lightDirection = gLight.Direction;
			gShaderModelData.viewMatrix = gMatrices.view;
			gShaderModelData.projectionMatrix = gMatrices.projection;

			gShaderModelData.ambientColor.x = 0.25f;
			gShaderModelData.ambientColor.y = 0.25f;
			gShaderModelData.ambientColor.z = 0.35f;
			gShaderModelData.ambientColor.w = 1;
			gShaderModelData.cameraPos.x = gCamera.worldMatrix.row4.x;
			gShaderModelData.cameraPos.y = gCamera.worldMatrix.row4.z;
			gShaderModelData.cameraPos.z = gCamera.worldMatrix.row4.y;
			gShaderModelData.cameraPos.w = gCamera.worldMatrix.row4.w;
		}

		// Create Vertex/Index Buffers

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
	}

	bool LoadDiffuseTextures()
	{
		VkQueue queue;
		VkCommandPool commandPool;
		VkPhysicalDevice physDevice;
		vlk.GetGraphicsQueue((void**)&queue);
		vlk.GetCommandPool((void**)&commandPool);
		vlk.GetPhysicalDevice((void**)&physDevice);

		// libktx, temporary variables
		ktxTexture* kTexture;
		KTX_error_code ktxResult;
		ktxVulkanDeviceInfo vlkDeviceInfo;

		// used to transfer texture CPU memory to GPU. just need one
		ktxResult = ktxVulkanDeviceInfo_Construct(&vlkDeviceInfo, physDevice, device, queue, commandPool, nullptr);
		if (ktxResult != KTX_error_code::KTX_SUCCESS)
			return false;

		// load all textures into CPU memory from file first
		unsigned int totalDiffuseCount = gLevelSelector.levelParser.levelInfo.totalDiffuseCount + 1;
		gDiffuseTextures.resize(totalDiffuseCount);
		gDiffuseTextureViews.resize(totalDiffuseCount);
		unsigned index = 0;
		unsigned maxLod = 0;

		ktxResult = CreateTexture(DEFAULT_DIFFUSE_MAP, &kTexture, vlkDeviceInfo, index, maxLod);
		if (ktxResult != KTX_error_code::KTX_SUCCESS)
		{
			std::cerr << "ERROR: LoadTextures - failed to load default diffuse map!\n";
			return false;
		}
		ktxTexture* kTexture2;
		for (auto graphicsObject : gObjects)
		{
			for (int i = 0; i < graphicsObject.materials.size(); i++)
			{
				std::string diffuseTextureStr = graphicsObject.diffuseTextures[i];
				if (diffuseTextureStr.compare("") != 0)
				{
					ktxResult = CreateTexture(diffuseTextureStr.c_str(), &kTexture2, vlkDeviceInfo, index, maxLod);
					if (ktxResult != KTX_error_code::KTX_SUCCESS)
					{
						std::cerr << "ERROR: LoadTextures - failed to load diffuse map (" << diffuseTextureStr << ")\n";
						return false;
					}
				}
			}
		}

		// Error check, ensure proper number of diffuse maps were read in.
		if (index != totalDiffuseCount)
		{
			std::cerr << "ERR: LoadTextures - diffuseMap count mismatch! (" << index <<
				" vs excepted " << gLevelSelector.levelParser.levelInfo.totalDiffuseCount << ")\n";
			return false;
		}

		// Create the sampler
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.flags = 0;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; // REPEAT IS COMMON
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0;
		samplerInfo.minLod = 0;
		samplerInfo.maxLod = maxLod;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.pNext = nullptr;

		VkResult vr = vkCreateSampler(device, &samplerInfo, nullptr, &gTextureSampler);
		if (vr != VkResult::VK_SUCCESS)
		{
			std::cerr << "ERROR: LoadTextures - Failed to create sampler!\n";
			return false;
		}

		// Then create image views for diffuse textures
		for (index = 0; index < gDiffuseTextures.size(); index++)
		{
			// Textures are not directly accessed by the shaders and are abstracted
			// by image views containing additional information and sub resource ranges.
			VkImageViewCreateInfo viewInfo = {};
			// Set the non-default values.
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.flags = 0;
			viewInfo.components = {
				VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
			};
			viewInfo.image = gDiffuseTextures[index].image;
			viewInfo.format = gDiffuseTextures[index].imageFormat;
			viewInfo.viewType = gDiffuseTextures[index].viewType;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.layerCount = gDiffuseTextures[index].layerCount;
			viewInfo.subresourceRange.levelCount = gDiffuseTextures[index].levelCount;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.pNext = nullptr;
			VkResult vr = vkCreateImageView(device, &viewInfo, nullptr, &(gDiffuseTextureViews[index]));
			if (vr != VkResult::VK_SUCCESS)
			{
				std::cerr << "ERROR: LoadTextures - Failed to create Image view (" << index << ")\n";
				return false;
			}

			VkWriteDescriptorSet write_descriptorset = {};
			write_descriptorset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptorset.descriptorCount = 1;
			write_descriptorset.dstArrayElement = 0;
			write_descriptorset.dstBinding = 0;
			write_descriptorset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write_descriptorset.dstSet = gDiffuseTextureDescriptorSets[index];
			VkDescriptorImageInfo diinfo = {
				gTextureSampler,
				gDiffuseTextureViews[index],
				gDiffuseTextures[index].imageLayout
			};
			write_descriptorset.pImageInfo = &diinfo;
			vkUpdateDescriptorSets(device, 1, &write_descriptorset, 0, nullptr);


		}

		// After loading all textures you don't need these anymore
		ktxTexture_Destroy(kTexture);
		ktxVulkanDeviceInfo_Destruct(&vlkDeviceInfo);

		return true;
	}

	KTX_error_code CreateTexture(const char* fileName, ktxTexture** kTexture, 
		ktxVulkanDeviceInfo vlkDeviceInfo, unsigned& index, unsigned& maxLod)
	{
		KTX_error_code ktxResult = ktxTexture_CreateFromNamedFile(fileName, KTX_TEXTURE_CREATE_NO_FLAGS, kTexture);
		if (ktxResult != KTX_error_code::KTX_SUCCESS)
			return ktxResult;

		// This gets mad if you don't encode/save the .ktx file in a format Vulkan likes
		ktxResult = ktxTexture_VkUploadEx(*kTexture, &vlkDeviceInfo, &(gDiffuseTextures[index]),
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (ktxResult != KTX_error_code::KTX_SUCCESS)
			return ktxResult;

		if (gDiffuseTextures[index].levelCount > maxLod)
			maxLod = gDiffuseTextures[index].levelCount;

		// Increment index to the next diffuse texture location
		++index;

		return KTX_error_code::KTX_SUCCESS;
	}

	void WriteModelsToShaderData()
	{
		unsigned int matrixOffset = 0;
		unsigned int materialOffset = 0;
		for (int i = 0; i < gObjects.size(); i++)
		{
			graphics::MODEL obj = gObjects[i];

			// Copy matrices
			memcpy(&(gShaderModelData.matrices[matrixOffset]), &(obj.worldMatrices[0]), sizeof(GW::MATH::GMATRIXF) * obj.instanceCount);
			matrixOffset += obj.instanceCount;

			// Copy materials
			for (int j = 0; j < obj.materialInfo.materialCount; j++)
			{
				memcpy(&(gShaderModelData.materials[materialOffset + j]), &(obj.materials[j].attrib), sizeof(graphics::ATTRIBUTES));
			}
			materialOffset += obj.materialInfo.materialCount;
		}

		//GvkHelper::write_to_buffer(device, gMatrixData[index], &gShaderModelData, sizeof(SHADER_MODEL_DATA));
	}

	void CleanUpVertexAndIndexBuffers()
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
	}

	void CleanUp()
	{
		CleanUpVertexAndIndexBuffers();
		
		for (VkBuffer& buffer : gMatrixBuffers)
			vkDestroyBuffer(device, buffer, nullptr);
		for (VkDeviceMemory& data : gMatrixData)
			vkFreeMemory(device, data, nullptr);
		for (VkImageView& imageView : gDiffuseTextureViews)
			vkDestroyImageView(device, imageView, nullptr);
		for (ktxVulkanTexture& textureData : gDiffuseTextures)
		{
			vkFreeMemory(device, textureData.deviceMemory, nullptr);
			vkDestroyImage(device, textureData.image, nullptr);
		}
		

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, pixelShader, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout_Vertex, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout_Pixel, nullptr);
		vkDestroySampler(device, gTextureSampler, nullptr);
		vkDestroyDescriptorPool(device, descPool, nullptr);
		
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};

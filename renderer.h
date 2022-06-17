#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#include <cmath>
#include "GraphicsObjects.h"
#include "LevelParser.h"
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

	std::vector<graphics::CAMERA> gCameras;
	graphics::CAMERA gCamera;
	GlobalMatrices gMatrices;
	Light gLight;

	std::vector<graphics::MODEL> gObjects;
	LevelParser::Selector gLevelSelector;

	// Shader Model Data sent to GPU
	SHADER_MODEL_DATA gShaderModelData;

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
		storageBuffers.resize(chainSwapCount);
		storageData.resize(chainSwapCount);
		for (unsigned int i = 0; i < chainSwapCount; i++)
		{
			GvkHelper::create_buffer(physicalDevice, device, sizeof(SHADER_MODEL_DATA),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storageBuffers[i], &storageData[i]);
		}
		WriteModelsToShaderData();

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
		constantRange.size = sizeof(PushConstants);
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
		
		// now we can draw
		unsigned int currentImageIndex;
		vlk.GetSwapchainCurrentImage(currentImageIndex);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &descSets[currentImageIndex], 0, nullptr);
		GvkHelper::write_to_buffer(device, storageData[currentImageIndex], &gShaderModelData, sizeof(SHADER_MODEL_DATA));


		VkDeviceSize offsets[] = { 0 };
		PushConstants pushConstants = { 0, 0 };
		unsigned int materialCount = 0;
		for (int i = 0; i < gObjects.size(); i++)
		{
			graphics::MODEL obj = gObjects[i];
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vkObjects[i].vertexHandle), offsets);
			vkCmdBindIndexBuffer(commandBuffer, vkObjects[i].indexHandle, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
			
			for (int j = 0; j < obj.meshCount; j++)
			{
				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(PushConstants), &pushConstants);
				vkCmdDrawIndexed(commandBuffer, obj.meshes[j].drawInfo.indexCount, obj.instanceCount, 
					obj.meshes[j].drawInfo.indexOffset, 0, 0);
				pushConstants.material_offset += 1;
			}

			materialCount += gObjects[i].materialCount;
			pushConstants.matrix_offset += obj.instanceCount;
		}
	}

	void CheckCommands()
	{
		float keyState;
		gInputProxy.GetState(G_KEY_L, keyState);
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
			for (int j = 0; j < obj.materialCount; j++)
			{
				memcpy(&(gShaderModelData.materials[materialOffset + j]), &(obj.materials[j].attrib), sizeof(graphics::ATTRIBUTES));
			}
			materialOffset += obj.materialCount;
		}

		//GvkHelper::write_to_buffer(device, storageData[index], &gShaderModelData, sizeof(SHADER_MODEL_DATA));
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

// Simple basecode showing how to create a window and attatch a vulkansurface
#define GATEWARE_ENABLE_CORE // All libraries need this
#define GATEWARE_ENABLE_SYSTEM // Graphics libs require system level libraries
#define GATEWARE_ENABLE_GRAPHICS // Enables all Graphics Libraries
#define GATEWARE_ENABLE_MATH

// Ignore some GRAPHICS libraries we aren't going to use
#define GATEWARE_DISABLE_GDIRECTX11SURFACE // we have another template for this
#define GATEWARE_DISABLE_GDIRECTX12SURFACE // we have another template for this
#define GATEWARE_DISABLE_GRASTERSURFACE // we have another template for this
#define GATEWARE_DISABLE_GOPENGLSURFACE // we have another template for this
// With what we want & what we don't defined we can include the API
#include "../Gateware/Gateware/Gateware.h"
#include "renderer.h"
#include "LevelParser.h"
// open some namespaces to compact the code a bit
using namespace GW;
using namespace CORE;
using namespace SYSTEM;
using namespace GRAPHICS;
// lets pop a window and use Vulkan to clear to a red screen
int main()
{
	LevelParser::Parser parser;
	parser.ParseGameLevel("../GameLevel.txt");

	parser.models.size();

	GWindow win;
	GEventResponder msgs;
	GVulkanSurface vulkan;
	if (+win.Create(0, 0, 800, 600, GWindowStyle::WINDOWEDBORDERED))
	{
		win.SetWindowName("Matthew Steinhardt - Level Renderer (Vulkan)");
		VkClearValue clrAndDepth[2];
		clrAndDepth[0].color = { {0.25f, 0.06f, 0.09f, 1} };
		clrAndDepth[1].depthStencil = { 1.0f, 0u };
		msgs.Create([&](const GW::GEvent& e) {
			GW::SYSTEM::GWindow::Events q;
			if (+e.Read(q) && q == GWindow::Events::RESIZE)
				clrAndDepth[0].color.float32[2] += 0.01f; // disable
			});
		win.Register(msgs);
#ifndef NDEBUG
		const char* debugLayers[] = {
			"VK_LAYER_KHRONOS_validation", // standard validation layer
			//"VK_LAYER_LUNARG_standard_validation", // add if not on MacOS
			"VK_LAYER_RENDERDOC_Capture" // add this if you have installed RenderDoc
		};
		if (+vulkan.Create(	win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT, 
							sizeof(debugLayers)/sizeof(debugLayers[0]),
							debugLayers, 0, nullptr, 0, nullptr, false))
#else
		if (+vulkan.Create(win, GW::GRAPHICS::DEPTH_BUFFER_SUPPORT))
#endif
		{
			//Renderer::ObjectData FSLogoObject = 
			//{
			//	FSLogo_vertices,
			//	FSLogo_vertexcount,
			//	FSLogo_indices,
			//	FSLogo_indexcount,
			//	FSLogo_materials,
			//	FSLogo_materialcount,
			//	FSLogo_meshes,
			//	FSLogo_meshcount
			//};

			//Renderer::ObjectData RaftObject =
			//{
			//	Raft_vertices,
			//	Raft_vertexcount,
			//	Raft_indices,
			//	Raft_indexcount,
			//	Raft_materials,
			//	Raft_materialcount,
			//	Raft_meshes,
			//	Raft_meshcount
			//};

			//Renderer renderer(win, vulkan, parser.models., 1);
			while (+win.ProcessWindowEvents())
			{
				if (+vulkan.StartFrame(2, clrAndDepth))
				{
					//renderer.Render();
					vulkan.EndFrame(true);
				}
			}
		}
	}
	return 0; // that's all folks
}

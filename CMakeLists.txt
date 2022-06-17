cmake_minimum_required(VERSION 3.10)

project(Level_Renderer_Vulkan)

# currently using unicode in some libraries on win32 but will change soon
ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)

# example of how to set the vertex shader properties inside of a cmake script and disable/enable building.
set_source_files_properties( MyVertexShader.hlsl PROPERTIES
        VS_SHADER_TYPE Vertex 
        VS_SHADER_MODEL 5.0
        VS_SHADER_ENTRYPOINT main
        VS_TOOL_OVERRIDE "None" 
	# Tip: Swap "None" for "FXCompile" to have them actually be compiled by VS.(Great for D3D11/12)
)

set(
	source_list
	"main.cpp"
	"LevelSelector.cpp"
	"renderer.h"
	"GraphicsObjects.h"
	"h2bParser.h"
	"LevelSelector.h"
)

set (
	shader_list
	"Shaders/VertexShader.hlsl"
	"Shaders/PixelShader.hlsl"
)

if (WIN32)
	set_property(SOURCE ${shader_list} PROPERTY VS_SETTINGS "ExcludedFromBuild=true")	
	# shaderc_combined.lib in Vulkan requires this for debug & release (runtime shader compiling)
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MD")
	add_executable (Level_Renderer_Vulkan ${source_list} ${shader_list})
	target_include_directories(Level_Renderer_Vulkan PUBLIC $ENV{VULKAN_SDK}/Include/)
	target_link_directories(Level_Renderer_Vulkan PUBLIC $ENV{VULKAN_SDK}/Lib/)

	source_group("Shaders"		FILES file(${shader_list}))
endif(WIN32)

if(UNIX AND NOT APPLE)
	# libshaderc_combined.a is required for runtime shader compiling
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -lX11 -lshaderc_combined")
    find_package(X11)
	find_package(Vulkan REQUIRED)
    link_libraries(${X11_LIBRARIES})
    include_directories(${X11_INCLUDE_DIR})
    include_directories(${Vulkan_INCLUDE_DIR}) 
	#link_directories(${Vulkan_LIBRARY}) this is currently not working
	link_libraries(${Vulkan_LIBRARIES})
	# the path is (properly)hardcoded because "${Vulkan_LIBRARY}" currently does not 
	# return a proper path on MacOS (it has the .dynlib appended)
    link_libraries(/usr/lib/x86_64-linux-gnu/libshaderc_combined.a)
    add_executable (Level_Renderer_Vulkan ${source_list} ${shader_list})
endif(UNIX AND NOT APPLE)

if(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fmodules -fcxx-modules")
	set(Architecture ${CMAKE_OSX_ARCHITECTURES})
	find_package(Vulkan REQUIRED)
	include_directories(${Vulkan_INCLUDE_DIR}) 
	#link_directories(${Vulkan_LIBRARY}) this is currently not working
	link_libraries(${Vulkan_LIBRARIES})
	# libshaderc_combined.a is required for runtime shader compiling
	# the path is (properly)hardcoded because "${Vulkan_LIBRARY}" currently does not 
	# return a proper path on MacOS (it has the .dynlib appended)
	link_libraries(/usr/local/lib/libshaderc_combined.a)
	add_executable (Level_Renderer_Vulkan main.mm)
endif(APPLE)

workspace "Velos"
	architecture "x86_64"
	startproject "Example_Triangle"

	configurations
	{
		"Debug",
		"Release"
	}

  multiprocessorcompile "On"

	outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

	IncludeDir = {}
	IncludeDir["Velos"] = "velos"
	IncludeDir["GLFW"]  = "external/glfw/include"
	IncludeDir["GLM"]   = "external/glm"
	IncludeDir["VOLK"]  = "external/volk"
	IncludeDir["VMA"]   = "external/vma/include"
	IncludeDir["STB"]   = "external/stb"
	IncludeDir["ImGui"] = "external/imgui"
  IncludeDir["SPIRVReflect"] = "external/SPIRV-Reflect"

project "Velos"
	location "build/Velos"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "velos/core/vlpch.h"
	pchsource "velos/core/vlpch.cpp"

	files
	{
		"velos/**.h",
		"velos/**.hpp",
		"velos/**.cpp",
		"velos/**.c",
    "external/SPIRV-Reflect/spirv_reflect.h",
    "external/SPIRV-Reflect/spirv_reflect.c"
	}

	includedirs
	{
		"%{IncludeDir.Velos}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
		"%{IncludeDir.ImGui}",
    "%{IncludeDir.SPIRVReflect}"
	}

	links
	{
		"GLFW"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE"
	}

	filter "system:windows"
		systemversion "latest"

		defines
		{
			"VL_PLATFORM_WINDOWS"
		}

		links
		{
			"vulkan-1.lib",
			"user32.lib",
			"gdi32.lib",
			"shell32.lib",
			"ole32.lib"
		}

	filter "system:linux"
		pic "On"

		defines
		{
			"VL_PLATFORM_LINUX"
		}

		links
		{
			"vulkan",
			"dl",
			"pthread",
			"X11",
			"Xrandr",
			"Xi",
			"Xxf86vm",
			"Xinerama",
			"Xcursor",
      "shaderc"
		}

	filter "configurations:Debug"
		defines
		{
			"VL_DEBUG"
		}
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines
		{
			"VL_RELEASE"
		}
		runtime "Release"
		optimize "Speed"

	filter {}

local exampleDirs = os.matchdirs("examples/*")

for _, dir in ipairs(exampleDirs) do
	local exampleName = path.getname(dir)
	local projectName = "Example_" .. exampleName
	local runProjectName = "Run_" .. exampleName

	project(projectName)
		location("build/" .. projectName)
		kind "ConsoleApp"
		language "C++"
		cppdialect "C++23"
		staticruntime "off"

		targetdir("bin/" .. outputdir .. "/%{prj.name}")
		objdir("bin-int/" .. outputdir .. "/%{prj.name}")

		files
		{
			dir .. "/**.h",
			dir .. "/**.hpp",
			dir .. "/**.cpp",
			dir .. "/**.c"
		}

		includedirs
		{
			"velos",
			dir,
			"%{IncludeDir.GLFW}",
			"%{IncludeDir.GLM}",
			"%{IncludeDir.VOLK}",
			"%{IncludeDir.VMA}",
			"%{IncludeDir.STB}",
      "%{IncludeDir.SPIRVReflect}"
		}

		links
		{
			"Velos"
		}

		filter "system:linux"
			links
			{
				"glfw",
				"vulkan",
				"shaderc_shared",
				"dl",
				"pthread",
				"X11",
				"Xrandr",
				"Xi",
				"Xxf86vm",
				"Xinerama",
				"Xcursor"
			}

		filter "configurations:Debug"
			defines { "VL_DEBUG" }
			runtime "Debug"
			symbols "On"

		filter "configurations:Release"
			defines { "VL_RELEASE" }
			runtime "Release"
			optimize "Speed"

		filter {}
end

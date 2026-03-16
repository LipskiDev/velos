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
		"velos/**.c"
	}

	includedirs
	{
		"%{IncludeDir.Velos}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
		"%{IncludeDir.ImGui}"
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
			"Xcursor"
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

project "Example_Triangle"
	location "build/Example_Triangle"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"examples/triangle/**.h",
		"examples/triangle/**.hpp",
		"examples/triangle/**.cpp"
	}

	includedirs
	{
		"velos",
		"examples/triangle",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}"
	}

	links
	{
		"Velos",
    "glfw"
	}

	filter "system:windows"
		systemversion "latest"

  filter "system:linux"
    links
    {
      "glfw",
      "vulkan",
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

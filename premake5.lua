workspace "Velos"
	architecture "x86_64"
	startproject "Example_triangle"

	configurations
	{
		"Debug",
		"Release"
	}

	multiprocessorcompile "On"

	outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

	local VulkanSDK = os.getenv("VULKAN_SDK")
	if os.host() == "windows" and (not VulkanSDK or VulkanSDK == "") then
		error("VULKAN_SDK is not set for Premake generation.")
	end

	IncludeDir = {}
	IncludeDir["Velos"] = "velos"
	IncludeDir["VelosCore"] = "velos/core"
	IncludeDir["GLFW"] = "external/glfw/include"
	IncludeDir["GLM"] = "external/glm"
	IncludeDir["VOLK"] = "external/volk"
	IncludeDir["VMA"] = "external/vma/include"
	IncludeDir["STB"] = "external/stb"
	IncludeDir["ImGui"] = "external/imgui"
	IncludeDir["SPIRVReflect"] = "external/SPIRV-Reflect"
	IncludeDir["Tracy"] = "external/tracy/public"

project "SPIRVReflect"
	location "build/SPIRVReflect"
	kind "StaticLib"
	language "C"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"external/SPIRV-Reflect/spirv_reflect.h",
		"external/SPIRV-Reflect/spirv_reflect.c"
	}

	includedirs
	{
		"%{IncludeDir.SPIRVReflect}"
	}

	filter "system:windows"
		systemversion "latest"

	filter "system:linux"
		pic "On"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		runtime "Release"
		optimize "Speed"

	filter {}

project "GLFW"
	location "build/GLFW"
	kind "StaticLib"
	language "C"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"external/glfw/src/**.h",
		"external/glfw/src/**.c"
	}

	includedirs
	{
		"external/glfw/include",
		"external/glfw/src"
	}

	filter "system:windows"
		systemversion "latest"

		files
		{
			"external/glfw/src/win32_*.*",
			"external/glfw/src/wgl_context.*",
			"external/glfw/src/egl_context.*",
			"external/glfw/src/osmesa_context.*"
		}

		defines
		{
			"_GLFW_WIN32"
		}

	filter "system:linux"
		pic "On"

		defines
		{
			"_GLFW_X11"
		}

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		runtime "Release"
		optimize "Speed"

	filter {}

project "Velos"
	location "build/Velos"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"velos/**.h",
		"velos/**.hpp",
		"velos/**.cpp"
	}

	includedirs
	{
		"%{IncludeDir.Velos}",
		"%{IncludeDir.VelosCore}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
		"%{IncludeDir.SPIRVReflect}"
	}

	links
	{
		"GLFW",
		"SPIRVReflect"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE"
	}

	filter "system:windows"
		systemversion "latest"

		includedirs
		{
			VulkanSDK .. "/Include"
		}

		libdirs
		{
			VulkanSDK .. "/Lib"
		}

		defines
		{
			"VL_PLATFORM_WINDOWS"
		}

		links
		{
			"vulkan-1",
			"user32",
			"gdi32",
			"shell32",
			"ole32"
		}

	filter "system:linux"
		files
		{
			"external/tracy/public/TracyClient.cpp"
		}

		includedirs
		{
			"%{IncludeDir.Tracy}"
		}

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

	filter { "system:linux", "configurations:Debug" }
		defines
		{
			"VL_DEBUG",
			"TRACY_ENABLE"
		}
		runtime "Debug"
		symbols "On"

	filter { "system:windows", "configurations:Debug" }
		defines
		{
			"VL_DEBUG"
		}
		links
		{
			"shaderc_combinedd"
		}
		runtime "Debug"
		symbols "On"

	filter { "system:windows", "configurations:Release" }
		defines
		{
			"VL_RELEASE"
		}
		links
		{
			"shaderc_combined"
		}
		runtime "Release"
		optimize "Speed"

	filter "configurations:Release"
		defines
		{
			"VL_RELEASE"
		}
		runtime "Release"
		optimize "Speed"

	filter {}

project "imgui"
	location "build/imgui"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"external/imgui/imgui.cpp",
		"external/imgui/imgui_draw.cpp",
		"external/imgui/imgui_tables.cpp",
		"external/imgui/imgui_widgets.cpp",
		"external/imgui/imgui_demo.cpp"
	}

	includedirs
	{
		"%{IncludeDir.ImGui}"
	}

	filter "system:linux"
		pic "On"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		runtime "Release"
		optimize "Speed"

	filter {}

project "VelosImGui"
	location "build/VelosImGui"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"tools/imgui/**.h",
		"tools/imgui/**.hpp",
		"tools/imgui/**.cpp",
		"tools/imgui/**.c"
	}

	includedirs
	{
		"velos",
		"velos/core",
		"tools/imgui",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
		"%{IncludeDir.SPIRVReflect}",
		"%{IncludeDir.Tracy}"
	}

	links
	{
		"Velos",
		"imgui"
	}

	filter "system:windows"
		systemversion "latest"

		includedirs
		{
			VulkanSDK .. "/Include"
		}

		libdirs
		{
			VulkanSDK .. "/Lib"
		}

		defines
		{
			"VL_PLATFORM_WINDOWS"
		}

		links
		{
			"vulkan-1",
			"user32",
			"gdi32",
			"shell32",
			"ole32"
		}

	filter "system:linux"
		pic "On"

		defines
		{
			"VL_PLATFORM_LINUX"
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
if not outputdir then
	error("outputdir must be defined before including external/velos/premake/embedded.lua")
end

IncludeDir = IncludeDir or {}
IncludeDir["Velos"] = "../velos"
IncludeDir["GLFW"] = "../external/glfw/include"
IncludeDir["GLM"] = "../external/glm"
IncludeDir["VOLK"] = "../external/volk"
IncludeDir["VMA"] = "../external/vma/include"
IncludeDir["STB"] = "../external/stb"
IncludeDir["ImGui"] = "../external/imgui"
IncludeDir["SPIRVReflect"] = "../external/SPIRV-Reflect"

project "Velos"
	location "../../build/Velos"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")

  pchheader "../velos/core/vlpch.h"
	pchsource "../velos/core/vlpch.cpp"

	files
	{
		"../velos/**.h",
		"../velos/**.hpp",
		"../velos/**.cpp",
		"../velos/**.c",
		"../external/SPIRV-Reflect/spirv_reflect.h",
		"../external/SPIRV-Reflect/spirv_reflect.c"
	}

	includedirs
	{
		"%{IncludeDir.Velos}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
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

project "imgui"
	location "../../build/imgui"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"../external/imgui/imgui.cpp",
		"../external/imgui/imgui_draw.cpp",
		"../external/imgui/imgui_tables.cpp",
		"../external/imgui/imgui_widgets.cpp",
		"../external/imgui/imgui_demo.cpp"
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
	location "../../build/VelosImGui"
	kind "StaticLib"
	language "C++"
	cppdialect "C++23"
	staticruntime "off"

	targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"../tools/imgui/**.h",
		"../tools/imgui/**.hpp",
		"../tools/imgui/**.cpp",
		"../tools/imgui/**.c"
	}

	includedirs
	{
		"../velos",
		"../tools/imgui",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLM}",
		"%{IncludeDir.VOLK}",
		"%{IncludeDir.VMA}",
		"%{IncludeDir.STB}",
		"%{IncludeDir.SPIRVReflect}"
	}

	links
	{
		"Velos",
		"imgui"
	}

	filter "system:linux"
		pic "On"

	filter "configurations:Debug"
		defines { "VL_DEBUG" }
		runtime "Debug"
		symbols "On"

	filter "configurations:Release"
		defines { "VL_RELEASE" }
		runtime "Release"
		optimize "Speed"

	filter {}

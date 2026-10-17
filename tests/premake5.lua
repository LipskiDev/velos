workspace "VelosTests"
  architecture "x86_64"
  configurations { "Debug", "Release" }
  startproject "VelosRhiTests"

local root = path.getabsolute("..")
local vulkanSdk = os.getenv("VULKAN_SDK")
if os.host() == "windows" and (not vulkanSdk or vulkanSdk == "") then
  error("VULKAN_SDK is not set.")
end

project "GlfwTest"
  kind "StaticLib"
  language "C"
  targetdir (root .. "/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/GlfwTest")
  objdir (root .. "/bin-int/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/GlfwTest")
  files { root .. "/external/glfw/src/**.c", root .. "/external/glfw/src/**.h" }
  removefiles {
    root .. "/external/glfw/src/cocoa_*",
    root .. "/external/glfw/src/glx_*",
    root .. "/external/glfw/src/linux_*",
    root .. "/external/glfw/src/posix_*",
    root .. "/external/glfw/src/wl_*",
    root .. "/external/glfw/src/x11_*",
    root .. "/external/glfw/src/xkb_*"
  }
  includedirs { root .. "/external/glfw/include", root .. "/external/glfw/src" }
  filter "system:windows"
    systemversion "latest"
    defines { "_GLFW_WIN32" }

project "VelosRhiTests"
  kind "ConsoleApp"
  language "C++"
  cppdialect "C++23"
  staticruntime "off"
  targetdir (root .. "/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/VelosRhiTests")
  objdir (root .. "/bin-int/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/VelosRhiTests")

  files {
    root .. "/tests/**.cpp",
    root .. "/velos/**.cpp",
    root .. "/velos/**.c",
    root .. "/external/SPIRV-Reflect/spirv_reflect.c"
  }

  includedirs {
    root,
    root .. "/velos",
    root .. "/velos/core",
    root .. "/external/glfw/include",
    root .. "/external/glm",
    root .. "/external/volk",
    root .. "/external/vma/include",
    root .. "/external/stb",
    root .. "/external/SPIRV-Reflect"
  }
  removefiles { root .. "/tests/d3d12/**.cpp" }

  defines { "_CRT_SECURE_NO_WARNINGS", "GLFW_INCLUDE_NONE" }
  links { "GlfwTest" }

  filter "system:windows"
    systemversion "latest"
    includedirs { vulkanSdk .. "/Include" }
    libdirs { vulkanSdk .. "/Lib" }
    defines { "VL_PLATFORM_WINDOWS", "VL_HAS_D3D12", "_GLFW_WIN32" }
    links { "vulkan-1", "d3d12", "dxgi", "dxguid", "dxcompiler", "user32", "gdi32", "shell32", "ole32" }
    postbuildcommands {
      '{COPYFILE} "' .. path.join(vulkanSdk, 'Bin', 'dxcompiler.dll') .. '" "%{cfg.targetdir}"',
      '{COPYFILE} "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxil.dll" "%{cfg.targetdir}"'
    }
  filter { "system:windows", "configurations:Debug" }
    links { "shaderc_combinedd" }
  filter { "system:windows", "configurations:Release" }
    links { "shaderc_combined" }

  filter "configurations:Debug"
    defines { "VL_DEBUG" }
    runtime "Debug"
    symbols "On"

  filter "configurations:Release"
    defines { "VL_RELEASE" }
    runtime "Release"
    optimize "Speed"

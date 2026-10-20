-- Force Windows target even when Premake runs on macOS/Linux
_OPTIONS["os"] = "windows"

local sdkdir = os.getenv("PLUGIN_SDK_DIR")
if not sdkdir or sdkdir == "" then
    error("ERROR: PLUGIN_SDK_DIR environment variable is not set!")
end
sdkdir = path.translate(sdkdir, "/")

workspace "SA.ImprovedShadows"
    configurations { "Release", "Debug" }
    location "build"

project "SA.ImprovedShadows"
    kind "SharedLib"
    language "C++"
    targetextension ".asi"

    architecture "x86"

    filter "action:gmake or action:gmake2"
        toolset "gcc"
        gccprefix "i686-w64-mingw32-"
        buildoptions {
            "-fpermissive",
            "-fcommon",
            "-fms-extensions",
            "-Wno-invalid-offsetof",
        }
        linkoptions {
            "-static-libgcc",
            "-static-libstdc++",
            "-Wl,-Bstatic,--whole-archive",
            "-lwinpthread",
            "-Wl,-Bdynamic,--no-whole-archive",
        }
    filter {}

    filter "files:**.cpp"
        buildoptions { "-std=c++2b" }
    filter {}

    includedirs {
        "source",
        sdkdir .. "/shared",
        sdkdir .. "/shared/game",
        sdkdir .. "/shared/dxsdk",
        sdkdir .. "/safetyhook",
        sdkdir .. "/plugin_sa",
        sdkdir .. "/plugin_sa/game_sa",
        sdkdir .. "/plugin_sa/game_sa/enums",
        sdkdir .. "/plugin_sa/game_sa/rw",
    }

    libdirs {
        sdkdir .. "/output/lib"
    }

    files {
        "source/**.h",
        "source/**.cpp",
    }

    filter "configurations:Release"
        defines { "GTASA", "PLUGIN_SGV_10US", "RW", "NDEBUG" }
        linkoptions { "-l:Plugin.a" }
        targetdir "output/Release"

    filter "configurations:Debug"
        defines { "GTASA", "PLUGIN_SGV_10US", "RW", "DEBUG", "_DEBUG" }
        linkoptions { "-l:Plugin_d.a" }
        targetdir "output/Debug"
    filter {}

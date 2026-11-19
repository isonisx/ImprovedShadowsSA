-- Force Windows target even when Premake runs on macOS/Linux
_OPTIONS["os"] = "windows"

local sdkdir = os.getenv("PLUGIN_SDK_DIR")
if not sdkdir or sdkdir == "" then
    error("ERROR: PLUGIN_SDK_DIR environment variable is not set!")
end
sdkdir = path.translate(sdkdir, "/")

workspace "ImprovedShadowsSA"
    configurations { "Release", "Debug" }
    location "build"

project "ImprovedShadowsSA"
    kind "SharedLib"
    language "C++"
    targetextension ".asi"

    architecture "x86"

    filter "action:gmake or action:gmake2"
        toolset "gcc"
        gccprefix "i686-w64-mingw32-"
        -- 32-bit MinGW otherwise does float math in x87 80-bit precision.
        -- MSVC x86 uses SSE2. Do not add -msseregparm; it breaks the Plugin.a ABI.
        buildoptions {
            "-msse2",
            "-mfpmath=sse",
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

    files { "source/**.cpp", "source/**.h", "doc/ImprovedShadowsSA.ini" }
    filter "files:doc/ImprovedShadowsSA.ini"
        buildmessage "Copying ImprovedShadowsSA.ini"
        local copy = os.host() == "windows" and "{COPYFILE}" or "cp"
        buildcommands { copy .. ' "%{file.relpath}" "%{cfg.targetdir}/ImprovedShadowsSA.ini"' }
        buildoutputs { "%{cfg.targetdir}/ImprovedShadowsSA.ini" }
    filter {}

    filter "configurations:Release"
        defines { "GTASA", "PLUGIN_SGV_10US", "RW", "NDEBUG" }
        linkoptions { "-l:Plugin.a" }
        targetdir "output/Release"

    filter "configurations:Debug"
        defines { "GTASA", "PLUGIN_SGV_10US", "RW", "DEBUG", "_DEBUG" }
        linkoptions { "-l:Plugin_d.a" }
        targetdir "output/Debug"
    filter {}

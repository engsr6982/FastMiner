add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
add_repositories("iceblcokmc https://github.com/IceBlcokMC/xmake-repo.git")


add_requires("levilamina 26.20.7", {configs = {target_type = get_config("target_type")}})
add_requires("ll-bstats 0.5.0", {configs = {target_type = get_config("target_type")}})

if is_config("target_type", "server") then
    add_requires("economy_bridge 2026.8.29")
end

add_requires("levibuildscript")
add_requires("abseil 20250127.0")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

if is_plat("windows") then
    set_toolchains("clang-cl") -- windows allways use clang-cl
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("FastMiner") -- Change this to your mod name.
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_defines("PLUGIN_NAME=\"FastMiner\"")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("cxx")
        add_cxflags("/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
    end
    add_files("src/**.cc")
    add_includedirs("src")
    add_packages("levilamina", "ll-bstats")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_packages("abseil")
    add_headerfiles("src/**.h")

    add_configfiles("BuildInfo.h.in")
    set_configdir("src/")

    if is_mode("debug") then
        add_defines("DEBUG")
    end

    if is_mode("release") then
        set_policy("build.optimization.lto", true) -- LTO
        set_optimize("fastest") -- O3 (LLVM)
    end 

    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
        add_packages("economy_bridge")
        add_files("src-server/**.cc")
        add_includedirs("src-server")
        add_headerfiles("src-server/**.h")
    else
        add_defines("LL_PLAT_C")
        add_files("src-client/**.cc")
        add_includedirs("src-client")
        add_headerfiles("src-client/**.h")
    end
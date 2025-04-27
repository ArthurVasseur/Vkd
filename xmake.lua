-- package("metrohash")
--     set_homepage("https://github.com/jandrewrogers/MetroHash")
--     set_description("Exceptionally fast and statistically robust hash functions.")
--     set_license("Apache License 2.0")

--     add_urls("git@github.com:jandrewrogers/MetroHash.git")
--     add_versions("1.1.3", "690a521d9beb2e1050cc8f273fdabc13b31bf8f6")

--     on_install(function (package)
--         io.writefile("xmake.lua", [[
--             target("metrohash")
--                 set_kind("$(kind)")
--                 set_languages("c++11")
--                 add_files("src/*.cpp")
--                 add_includedirs("src", { public = true })
--                 add_headerfiles("src/(*.h)")
--                 add_cxflags("-msse4.2", "-mcrc32")

--         ]])

--         import("package.tools.xmake").install(package)
--     end)

--     on_test(function (package)
--         assert(package:check_cxxsnippets({test = [[
--             #include <metrohash.h>
--             void test() {
--                 MetroHash64 hash;
--                 hash.Initialize(99);
--             }
--         ]]}, {configs = {languages = "c++17"}}))
--     end)
-- package_end()

-- package("CWPack")
--     set_homepage("https://github.com/clwi/CWPack")
--     set_description("An ANSI C encoder/decoder for the MessagePack serialization format / msgpack.org[C / Objective-C / Swift]")
--     set_license("MIT")

--     add_urls("git@github.com:clwi/CWPack.git")
--     add_versions("1.4", "833fec93903f047ae5c47936f884ba27fc4c7a4c")

--     on_install(function (package)
--         io.writefile("xmake.lua", [[
--             target("metrohash")
--                 set_kind("$(kind)")
--                 set_languages("c99")
--                 add_files("src/*.c")
--                 add_includedirs("src", { public = true })
--                 add_headerfiles("src/(*.h)")
--         ]])

--         import("package.tools.xmake").install(package)
--     end)

--     on_test(function (package)
--         assert(package:check_csnippets({test = [[
--             #include <cwpack.h>
--             void test() {
--                 cw_pack_context pc;
--                 char buffer[20];
--                 cw_pack_context_init (&pc, buffer, 20, 0);
--             }
--         ]]}, {configs = {languages = "c99"}}))
--     end)
-- package_end()

-- package("pal")
--     set_homepage("https://github.com/GPUOpen-Drivers/pal")
--     set_description("The Platform Abstraction Library (PAL) provides hardware and OS abstractions for Radeon™")
--     set_license("MIT")

--     add_urls("git@github.com:GPUOpen-Drivers/pal.git")
--     add_versions("914", "04bc1e796dd15fc90fff8fa826d32e431d8722f6")
--     add_patches("914", path.join(os.scriptdir(), "pal-914.patch"), "f25e93cbeb0e13e13fe7a8b8b52395776b696cba5e36fda6ca50a6db178f66e4")

--     add_configs("jemalloc", { description = "Enable jemalloc support", default = false, type = "boolean" })
--     add_configs("prints_asserts", { description = "Enable prints and asserts", default = false, type = "boolean" })
--     add_configs("lto", { description = "Enable LTO support", default = false, type = "boolean" })
--     add_configs("memtrack", { description = "Enable memory tracking", default = false, type = "boolean" })

--     add_deps("metrohash", "CWPack")

--     on_install(function (package)

--         local configs = {}

--         local metrohash = package:dep("metrohash")
--         if metrohash then
--             table.insert(configs, "-DXMAKE_METRO_HASH=" .. metrohash:installdir())
--         end

--         local cwpack = package:dep("CWPack")
--         if cwpack then
--             table.insert(configs, "-DXMAKE_CWPACK=" .. cwpack:installdir())
--         end

--         table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
--         table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
--         table.insert(configs, "-DPAL_CLIENT_INTERFACE_MAJOR_VERSION=914")
--         table.insert(configs, "-DPAL_CLIENT=VULKAN")
--         table.insert(configs, "-DPAL_ENABLE_JEMALLOC=" .. (package:config("jemalloc") and "ON" or "OFF"))
--         table.insert(configs, "-DPAL_ENABLE_PRINTS_ASSERTS=" .. (package:config("prints_asserts") and "ON" or "OFF"))
--         table.insert(configs, "-DPAL_ENABLE_LTO=" .. (package:config("lto") and "ON" or "OFF"))
--         table.insert(configs, "-DPAL_ENABLE_MEMTRACK=" .. (package:config("memtrack") and "ON" or "OFF"))
--         table.insert(configs, "-DCMAKE_CXX_STANDARD=20")

--         import("package.tools.cmake").install(package, configs)
--    end)

-- package_end()



add_rules("mode.debug", "mode.release")
add_repositories("Concerto-xrepo https://github.com/ConcertoEngine/xmake-repo.git main")
add_requires("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")

target("vkd")
    set_languages("cxx20")
    set_kind("shared")
    add_files("Src/**.cpp")
    add_includedirs("Src", { public = true })
    add_headerfiles("Src/(Vkd/**.hpp)", "Src/(Vkd/**.inl)")
    add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_defines("VK_NO_PROTOTYPES")
    if is_plat("windows") then
        add_syslinks("Gdi32")
    end
    after_build(function(target)
        local lib_path = path.absolute(target:targetfile())
        local json_content = string.format([[
        {
            "file_format_version": "1.0.1",
            "ICD": {
                "library_path": %q,
                "api_version": "1.4.304",
                "library_arch" : "64",
                "is_portability_driver": false
            }
        },
        ]], lib_path)

        io.writefile("vkd.json", json_content)
    end)
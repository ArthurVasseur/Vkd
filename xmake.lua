add_rules("mode.debug", "mode.release")
add_repositories("Concerto-xrepo https://github.com/ConcertoEngine/xmake-repo.git main")
add_requires("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")

target("vkd")
    set_languages("cxx20")
    set_kind("shared")
    add_files("Src/Vkd/**.cpp")
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
target_end()


if is_plat("windows") then
    add_requires("directx-headers", "microsoft-detours", "nlohmann_json")

    target("wddm-dump")
        set_languages("cxx20")
        set_kind("binary")
        add_files("Src/WddmDump/**.cpp")
        add_includedirs("Src", { public = true })
        add_headerfiles("Src/(WddmDump/**.hpp)", "Src/(WddmDump/Api/**.hpp)")
        add_packages("directx-headers", "concerto-core", "microsoft-detours", "nlohmann_json")
        add_syslinks("d3d12", "dxgi", "d3dcompiler", "dxguid")

        on_config(function(target)
            local out_folder = target:autogendir()
            local out_hpp_file = path.join(out_folder, "WddmDump", "WddmFunction.hpp")
            local out_cpp_file = path.join(out_folder, "WddmDump", "WddmFunction.cpp")
            os.execv("python.exe", { "./gen_wddm_dump.py", path.absolute("./wddm.json"), path.absolute(out_hpp_file), path.absolute(out_cpp_file)})
            local header_file = target:autogendir() .. "/(WddmDump/*.hpp)"
            target:add("headerfiles", header_file, out_hpp_file)
            target:add("includedirs", out_folder, {public = true})
            target:add("files", out_cpp_file, {public = true})
            
            -- local windows_kit = "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared"
            -- local d3dukmdt = path.join(windows_kit, "d3dukmdt.h")
            -- local d3dkmthk = path.join(windows_kit, "d3dkmthk.h")
            -- local d3dkmdt = path.join(windows_kit, "d3dkmdt.h")

            -- out_hpp_file = path.join(out_folder, "WddmDump", "WddmJson.hpp")
            -- os.execv("python.exe", { "./gen_wddm_to_json.py", d3dukmdt, d3dkmthk, d3dkmdt, out_hpp_file})
            -- target:add("headerfiles", out_hpp_file, {public = true})
        end)
end
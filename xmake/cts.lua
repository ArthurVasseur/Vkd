if has_config("cts") then
    package("vk-gl-cts")
        set_homepage("https://github.com/KhronosGroup/VK-GL-CTS")
        set_description("Khronos Vulkan Conformance Test Suite")
        set_license("Apache-2.0")

        add_urls(
            "https://data.arthurvasseur.fr/vk-gl-cts/vk-gl-cts-$(plat)-$(version).1.zip"
        )
        
        if is_plat("windows") then
            add_versions("1.4.4", "d82631c355351c96ec10aa4a9eaf7701c661a42d6347c3a169357be27878936b")
        elseif is_plat("linux") then
            add_versions("1.4.4", "82911bedc15138a07526a15c9598009404f2d5f7e8d8ef965b2884dcc8680d63")
        end

        on_install(function (package)
            os.cp("bin/*", package:installdir("bin"))
            os.cp("scripts/*", package:installdir("scripts"))
        end)

        on_test(function (package)
            import("lib.detect.find_tool")
            local deqp_vk = find_tool("deqp-vk", {paths = {path.join(package:installdir("bin"))}, check = "-h"})
            assert(deqp_vk, "deqp-vk not found!")
        end)
    package_end()

    add_requires("python 3.x")

    for driver_name, driver in pairs(drivers) do
        target("vk-cts-" .. driver_name)
            set_kind("phony")
            add_packages("vk-gl-cts", "python")
            add_deps("vkd-" .. driver_name)

            on_run(function (target)
                import("lib.detect.find_tool")
                
                local vk_gl_cts = target:pkg("vk-gl-cts")
                if not vk_gl_cts then
                    raise("VK-GL-CTS package not found. Please run 'xmake f --cts=y' and rebuild.")
                end

                local deqp_vk = find_tool("deqp-vk", {paths = {path.join(vk_gl_cts:installdir(), "bin")}, check = "-h"})
                assert(deqp_vk, "deqp-vk not found in VK-GL-CTS package!")

                local envs = os.joinenvs(os.getenvs(), {
                    VK_DRIVER_FILES = path.absolute("vkd-" .. driver_name .. ".json"),
                    VK_LOADER_LAYERS_DISABLE = "~implicit~",
                    --VK_LOADER_DEBUG = "all",
                })

                local output_dir = path.join(os.scriptdir(), driver_name .. "-cts-results/")
                if os.isdir(output_dir) then
                    os.rm(output_dir)
                end

                os.mkdir(output_dir)

                local bin_dir = path.join(vk_gl_cts:installdir(), "bin")
                os.cd(bin_dir)

                local ret = os.execv(deqp_vk.program, {
                        "--deqp-archive-dir=" .. output_dir,
                        "--deqp-shadercache-filename=" .. path.join(output_dir, "vk-cts-shadercache.bin"),
                        "--deqp-log-filename=" .. path.join(output_dir, "vk-cts-log.txt"),
                        "--deqp-caselist-file=" .. path.join(os.scriptdir(), "vk-default.txt"),
                    },
                    {envs = envs, try = true}
                )

                os.cd("-")
                
                local python = find_tool("python")
                assert(python, "python not found!")

                os.vrunv(python.program, {"-m", "pip", "install", "pandas"})
            
                local report_script = path.join(vk_gl_cts:installdir(), "scripts", "log/log_to_xml.py")
                os.vrunv(python.program, {report_script, "./" .. driver_name .. "-cts-results/vk-cts-log.txt", "./" .. driver_name .. "-cts-results/vk-cts-report.xml"})

                local reporter_script = path.join(os.scriptdir(), "scripts", "cts_report.py")
                os.vrunv(python.program, {reporter_script, "./" .. driver_name .. "-cts-results/vk-cts-report.xml", "./" .. driver_name .. "-cts-results/result.html"})
            end)
        target_end()
    end
end

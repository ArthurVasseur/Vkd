add_rules("mode.debug", "mode.release")
add_repositories("Concerto-xrepo https://github.com/ConcertoEngine/xmake-repo.git main")
add_repositories("nazara-repo https://github.com/NazaraEngine/xmake-repo")
add_requires("vulkan-headers", "vulkan-utility-libraries", "mimalloc", "nazarautils", "catch2", "volk")
add_requires("concerto-core", {configs = {asserts = get_config("debug_checks"), debug = is_mode("debug")}})
add_requires("catch2")

option("debug_checks", {default = is_mode("debug"), description = "Enable additional debug checks"})
option("profiling", { description = "Build with tracy profiler", default = false })
option("tests", { description = "Build test applications", default = true })
option("cts", { description = "Build Vulkan CTS", default = false })

if has_config("profiling") then
	add_requires("tracy")
end

if has_config("cts") then
	add_requires("vk-gl-cts")
end


function add_files_to_target(p, hpp_as_files)
    for _, dir in ipairs(os.filedirs(p)) do
        relative_dir = path.relative(dir, "Src/")
        if os.isdir(dir) then
            add_files(path.join("Src", relative_dir, "*.cpp"))
            if hpp_as_files then
                add_files(path.join("Src", relative_dir, "*.hpp"))
            end
            add_headerfiles(path.join("Src", "(" .. relative_dir .. "/*.hpp)"))
            add_headerfiles(path.join("Src", "(" .. relative_dir .. "/*.inl)"))
        else
            local ext = path.extension(relative_dir)
            if ext == ".hpp" or ext == ".inl" then
                add_headerfiles(path.join("Src", "(" .. relative_dir .. ")"))
            elseif ext == ".cpp" then
                add_files(path.join("Src", relative_dir))
            end
        end
    end
end

local drivers = {
    Software = {
        Files = {
            ".",
            "Buffer",
            "CommandBuffer",
            "CommandDispatcher",
            "CommandPool",
            "CpuContext",
            "Device",
            "DeviceMemory",
            "PhysicalDevice",
            "Pipeline",
            "Queue",
            "Synchronization",
            "Synchronization/Fence",
        },
        Packages = { {"concerto-core", public = false}, {"vulkan-headers", public = true}},
        Deps = {},
    }
}

target("vkd-Utils")
    set_languages("c++20")
    set_kind("static")
    add_includedirs("Src", { public = true })
    add_packages("concerto-core", "mimalloc", {public = true})

    if is_plat("linux", "macosx", "bsd") then
        add_cxflags("-fPIC")
    end

    local files = {
        ".",
        "Allocator",
        "Memory",
        "System",
        "ThreadPool",
    }
    for _, dir in ipairs(files) do
        add_files_to_target("Src/VkdUtils/" .. dir, false)
    end

    if is_plat("mingw", "linux", "macosx", "bsd") then
        add_syslinks("pthread")
    end

    -- macOS: ensure we link against the correct C++ runtime when using custom toolchain
    if is_plat("macosx") then
        local llvm_prefix = os.getenv("LLVM_PREFIX")
        if llvm_prefix then
            add_linkdirs(path.join(llvm_prefix, "lib"))
            add_rpathdirs(path.join(llvm_prefix, "lib"))
        end
    end

target("vkd")
    set_languages("c++20")
    set_kind("static")
    add_files("Src/Vkd/**.cpp")
    add_includedirs("Src", { public = true })
    add_headerfiles("Src/(Vkd/**.hpp)", "Src/(Vkd/**.inl)")
    add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_packages("nazarautils", {public = true})
    add_defines("VK_NO_PROTOTYPES")
    add_deps("vkd-Utils", { public = true })

    if is_plat("linux", "macosx", "bsd") then
        add_cxflags("-fPIC")
    end
    if is_plat("windows") then
        add_syslinks("Gdi32", "SetupAPI")
    end
    if has_config("debug_checks") then
        add_defines("VKD_DEBUG_CHECKS", { public = true })
    end
    if has_config("profiling") then
        add_packages("tracy", {public = true})
        add_defines("VKD_PROFILING", { public = true })
    end

    local files = {
        ".",
        "Buffer",
        "CommandBuffer",
        "CommandPool",
        "Device",
        "DeviceMemory",
        "Icd",
        "Image",
        "Instance",
        "Memory",
        "ObjectBase",
        "PhysicalDevice",
        "Pipeline",
        "Queue",
        "Synchronization",
        "Synchronization/Fence",
    }
    for _, dir in ipairs(files) do
        add_files_to_target("Src/Vkd/" .. dir, false)
    end
target_end()

for driver_name, driver in pairs(drivers) do
    target("vkd-" .. driver_name)
        set_kind("shared")
        set_languages("c++20")
        add_defines("VKD_" .. driver_name:upper() .. "_BUILD", { public = false })
        add_includedirs("Src/", { public = true })
        add_defines("VK_NO_PROTOTYPES")
        add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries")
        for _, pkg in ipairs(driver.Packages) do
            add_packages(pkg)
        end

        for _, dep in ipairs(driver.Deps) do
            add_deps(dep, { public = true })
        end
        
        if is_mode("debug") then
            set_symbols("debug")
        end

        for _, dir in ipairs(driver.Files) do
            add_files_to_target("Src/Vkd" .. driver_name .. "/" .. dir, false)
        end
        add_deps("vkd")
        
        if is_plat("mingw", "linux", "macosx", "bsd") then
            add_syslinks("pthread")
        end

        -- macOS: ensure we link against the correct C++ runtime when using custom toolchain
        if is_plat("macosx") then
            local llvm_prefix = os.getenv("LLVM_PREFIX")
            if llvm_prefix then
                add_linkdirs(path.join(llvm_prefix, "lib"))
                add_rpathdirs(path.join(llvm_prefix, "lib"))
            end
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
            }
            ]], lib_path)

            io.writefile("vkd-" .. driver_name .. ".json", json_content)
            target:add("installfiles", lib_path)
        end)
    target_end()
end

if has_config("tests") then
    target("vkd-tests")
        set_languages("c++20")
        set_kind("binary")
        add_includedirs("Src", { public = true })
        add_packages("catch2")
        add_files("Src/Tests/**.cpp")
        add_deps("vkd-Utils")
    target_end()

    target("vkd-test-app")
        set_languages("c++20")
        set_kind("binary")
        add_files("Tests/**.cpp")
        add_includedirs("Src", { public = true })
        add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc", "volk")
        add_defines("VK_NO_PROTOTYPES")
        add_files("Src/TestApp/main.cpp")

        for driver_name, driver in pairs(drivers) do
            add_deps("vkd-" .. driver_name)
        end
    target_end()
end

if has_config("cts") then
    package("vk-gl-cts")
        set_homepage("https://github.com/KhronosGroup/VK-GL-CTS")
        set_description("Khronos Vulkan Conformance Test Suite")
        set_license("Apache-2.0")

        add_urls(
            "https://data.arthurvasseur.fr/vk-gl-cts/vk-gl-cts-$(version).1.zip"
        )
        
        add_versions("1.4.4", "d82631c355351c96ec10aa4a9eaf7701c661a42d6347c3a169357be27878936b")

        on_install(function (package)
            os.trycp("bin/*.exe", package:installdir("bin"))
            os.trycp("scripts/*", package:installdir("scripts"))
        end)

        on_test(function (package)
            local deqp_vk = path.join(package:installdir("bin"), "deqp-vk" .. (is_host("windows") and ".exe" or ""))
            assert(os.isfile(deqp_vk), "deqp-vk executable not found")
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
                    VK_DRIVER_FILES = path.absolute("vkd-" .. driver_name .. ".json")
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
                        "-n",
                        "dEQP-VK.info.*"
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
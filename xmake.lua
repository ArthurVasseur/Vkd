add_rules("mode.debug", "mode.release")
add_repositories("Concerto-xrepo https://github.com/ConcertoEngine/xmake-repo.git main")
add_repositories("nazara-repo https://github.com/NazaraEngine/xmake-repo")
add_requires("vulkan-headers", "vulkan-utility-libraries", "mimalloc", "concerto-graphics", "nazarautils", "catch2")
add_requires("concerto-core", {configs = {asserts = get_config("debug_checks"), debug = is_mode("debug")}})
add_requires("catch2")

option("debug_checks", {default = is_mode("debug"), description = "Enable additional debug checks"})
option("profiling", { description = "Build with tracy profiler", default = false })

if has_config("profiling") then
    add_requires("tracy")
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
    set_languages("cxx20")
    set_kind("static")
    add_includedirs("Src", { public = true })
    add_packages("concerto-core", "mimalloc", {public = true})

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

target("vkd")
    set_languages("cxx20")
    set_kind("static")
    add_files("Src/Vkd/**.cpp")
    add_includedirs("Src", { public = true })
    add_headerfiles("Src/(Vkd/**.hpp)", "Src/(Vkd/**.inl)")
    add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_packages("nazarautils", {public = true})
    add_defines("VK_NO_PROTOTYPES")
    add_deps("vkd-Utils", { public = true })
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
        set_languages("cxx20")
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

            io.writefile("vkd-" .. driver_name .. ".json", json_content)
        end)
    target_end()
end

target("vkd-test")
    set_languages("cxx20")
    set_kind("binary")
    add_includedirs("Src", { public = true })
    add_packages("catch2")
    add_files("Src/Tests/**.cpp")
    add_deps("vkd-Utils")
target_end()

target("vkd-test")
    set_languages("cxx20")
    set_kind("binary")
    add_files("Tests/**.cpp")
    add_includedirs("Src", { public = true })
    add_packages("concerto-core", "concerto-graphics", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_defines("VK_NO_PROTOTYPES")
    add_files("Src/TestApp/main.cpp")

    for driver_name, driver in pairs(drivers) do
        add_deps("vkd-" .. driver_name)
    end

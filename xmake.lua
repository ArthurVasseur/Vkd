add_rules("mode.debug", "mode.release")
add_repositories("Concerto-xrepo https://github.com/ConcertoEngine/xmake-repo.git main")
add_requires("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc", "concerto-graphics")

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

target("vkd")
    set_languages("cxx20")
    set_kind("shared")
    add_files("Src/Vkd/**.cpp")
    add_includedirs("Src", { public = true })
    add_headerfiles("Src/(Vkd/**.hpp)", "Src/(Vkd/**.inl)")
    add_packages("concerto-core", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_defines("VK_NO_PROTOTYPES")
    if is_plat("windows") then
        add_syslinks("Gdi32", "SetupAPI")
    end

    local files = {
        ".",
        "Device",
        "Icd",
        "Instance",
        "Memory",
        "ObjectBase",
        "PhysicalDevice",
        "SoftwareDevice",
        "SoftwarePhysicalDevice"
    }
    for _, dir in ipairs(files) do
        add_files_to_target("Src/Vkd/" .. dir, false)
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

target("vkd-test")
    set_languages("cxx20")
    set_kind("binary")
    add_files("Tests/**.cpp")
    add_includedirs("Src", { public = true })
    add_packages("concerto-core", "concerto-graphics", "vulkan-headers", "vulkan-utility-libraries", "mimalloc")
    add_defines("VK_NO_PROTOTYPES")
    add_deps("vkd")
    add_files("Src/Test/*.cpp")


if has_config("installer") then
    includes("@builtin/xpack")

    local driver_targets = {}
    for driver_name, _ in pairs(drivers) do
        table.insert(driver_targets, "vkd-" .. driver_name)
    end

    xpack("vkd")
        set_formats("nsis", "zip", "targz")
        set_title("VKD Software Drivers")
        set_description("VKD Vulkan software drivers")
        set_author("Arthur Vasseur")
        set_version("0.1.0")
        add_targets(table.unpack(driver_targets))

        before_package(function (package)
            import("core.project.project")

            for driver_name, _ in pairs(drivers) do
                local t = project.target("vkd-" .. driver_name)
                if t then
                    local dll_name = path.filename(t:targetfile())
                    local json_filename = "vkd-" .. driver_name .. ".json"
                    local json_path = path.join(rootdir, json_filename)
                    local content = string.format([[
    {
        "file_format_version": "1.0.1",
        "ICD": {
            "library_path": %q,
            "api_version": "1.4.304",
            "library_arch" : "64",
            "is_portability_driver": false
        }
    }
    ]], path.join(".", "bin", dll_name))

                    io.writefile(json_path, content)
                    package:add("installfiles", json_path, {
                        filename  = json_filename
                    })
                end
            end
        end)

        after_installcmd(function (package, batchcmds)
            for driver_name, _ in pairs(drivers) do
                batchcmds:rawcmd("nsis", string.format([[
    ${If} $NoAdmin == "false"
        SetRegView 64
        WriteRegDWORD ${HKLM} "Software\Khronos\Vulkan\Drivers" "$InstDir\vkd-%s.json" 0
    ${EndIf}
    ]], driver_name))
            end
        end)

        after_uninstallcmd(function (package, batchcmds)
            for driver_name, _ in pairs(drivers) do
                batchcmds:rawcmd("nsis", string.format([[
    ${If} $NoAdmin == "false"
        SetRegView 64
        DeleteRegValue ${HKLM} "Software\Khronos\Vulkan\Drivers" "$InstDir\vkd-%s.json"
    ${EndIf}
    ]], driver_name))
            end
        end)
end
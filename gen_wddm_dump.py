import json
import os

def generate_hpp_from_json(json_path: str, hpp_path: str):
    with open(json_path, 'r') as f:
        data = json.load(f)
    functions = []
    for block in data.values():
        for fn in block['functions']:
            functions.append(fn)
    with open(hpp_path, 'w') as hpp:
        hpp.write('#pragma once\n')
        hpp.write('#include <windows.h>\n')
        hpp.write('#include <d3dkmthk.h>  // for D3DKMT_* types\n')
        hpp.write('#include <Concerto/Core/Logger.hpp>\n\n')
        hpp.write('#include <Concerto/Core/Assert.hpp>\n\n')
        hpp.write('#include <unordered_map>\n')
        hpp.write('#include <nlohmann/json.hpp>\n')
        hpp.write('#include <detours.h>\n\n')
        hpp.write('nlohmann::json& GetWddmJson();\n\n')
        for fn in functions:
            name = fn['name']
            ret = fn['returns']
            params = fn['parameters']

            sig_params = []
            for p in params:
                ptype = p['type']
                sig_params.append(f'{ptype}')
            sig = ', '.join(sig_params)

            hook_name = f'Hooked{name}'

            hpp.write(f'{ret} APIENTRY {hook_name}({sig});\n')
            hpp.write(f'using PFN_{name} = {ret}(APIENTRY*)({sig});\n')        

        hpp.write(f'\n\n')
        hpp.write('void LoadWddmFunctions(HMODULE hGdi32);\n')
        hpp.write('void AttachWddmToDetour();\n')
        hpp.write('void DetachWddmFromDetour();\n\n')
       

def generate_cpp_from_json(json_path: str, cpp_path: str):
    with open(json_path, 'r') as f:
        data = json.load(f)

    functions = []
    for block in data.values():
        for fn in block['functions']:
            functions.append(fn)

    with open(cpp_path, 'w') as cpp:
        cpp.write('#include <unordered_map>\n')
        cpp.write('#include <string>\n')
        cpp.write('#include <mutex>\n')
        cpp.write('#include <windows.h>\n')
        cpp.write('#include <d3dkmthk.h>  // for D3DKMT_* types\n')
        cpp.write('#include <Concerto/Core/Logger.hpp>\n')
        cpp.write('#include "WddmDump/WddmFunction.hpp"\n')
        cpp.write('#include "WddmDump/WddmJson.hpp"\n')
        cpp.write('\n\n')

        cpp.write('nlohmann::json g_wddm_json = nlohmann::json::array();\n')
        cpp.write('nlohmann::json& GetWddmJson()\n{\n')
        cpp.write('return g_wddm_json;\n')
        cpp.write('}\n\n')
        for fn in functions:
            name = fn['name']
            cpp.write(f'PFN_{name} g_{name} = nullptr;\n')
        cpp.write('\n\n')
        cpp.write('void LoadWddmFunctions(HMODULE hGdi32)\n{\n')
        for fn in functions:
            name = fn['name']
            cpp.write(f'    g_{name} = reinterpret_cast<PFN_{name}>(GetProcAddress(hGdi32, "{name}"));\n')
        cpp.write('}\n\n')


        cpp.write('void AttachWddmToDetour()\n{\n')
        cpp.write('    DetourTransactionBegin();\n')
        cpp.write('    DetourUpdateThread(GetCurrentThread());\n')
        cpp.write('    NTSTATUS status;\n')
        for fn in functions:
            name = fn['name']
            hook_name = f'Hooked{name}'
            cpp.write(f'    if (g_{name}) {{\n')
            cpp.write(f'        status = DetourAttach(&g_{name}, {hook_name});\n')
            cpp.write('        if (status != NO_ERROR)\n')
            cpp.write('            CCT_ASSERT_FALSE("Invalid status");\n')
            cpp.write('    }\n')
        cpp.write('    DetourTransactionCommit();\n')
        cpp.write('}\n\n')
        
        # Generate Undetour function
        cpp.write('void DetachWddmFromDetour()\n{\n')
        cpp.write('    DetourTransactionBegin();\n')
        cpp.write('    DetourUpdateThread(GetCurrentThread());\n')
        for fn in functions:
            name = fn['name']
            hook_name = f'Hooked{name}'
            cpp.write(f'    if (g_{name}) {{\n')
            cpp.write(f'        DetourDetach(&g_{name}, {hook_name});\n')
            cpp.write('    }\n')
        cpp.write('    DetourTransactionCommit();\n')
        cpp.write('}\n\n')

        for fn in functions:
            name = fn['name']
            ret = fn['returns']
            params = fn['parameters']

            sig_params = []
            call_args = []
            param_index = 0
            for p in params:
                ptype = p['type']
                pname = f'param{param_index}'
                sig_params.append(f'{ptype} {pname}')
                call_args.append(pname)
                param_index += 1
            sig = ', '.join(sig_params)
            args = ', '.join(call_args)

            hook_name = f'Hooked{name}'

            cpp.write(f'{ret} {hook_name}({sig}) {{\n')
            cpp.write(f'    nlohmann::json j = {{ "{name}", {{\n')
            for i, p in enumerate(params):
                pname = f'param{i}'
                if p['type'] == 'HANDLE':
                    cpp.write(f'        {{"{pname}", std::format("{{}}", {pname})}},\n')
                elif p['type'] == 'HANDLE*':
                    cpp.write(f'        {{"{pname}", std::format("{{}}", *{pname})}},\n')
                else:
                    cpp.write(f'        {{"{p["type"]}", {"*" + pname if "*" in p["type"] else pname}}},\n')
            cpp.write('    } };\n')
            cpp.write('    GetWddmJson().push_back(j);\n')
            cpp.write(f'    return g_{name}({args});\n')
            cpp.write('}\n\n')

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Generate C++ WDDM hook stubs from JSON spec.")
    parser.add_argument('json', help='Path to input JSON file')
    parser.add_argument('hpp', help='Path to output header file')
    parser.add_argument('cpp', help='Path to output C++ file')
    args = parser.parse_args()

    if not os.path.exists(args.hpp):
        os.makedirs(os.path.dirname(args.hpp), exist_ok=True)
    if not os.path.exists(args.cpp):
        os.makedirs(os.path.dirname(args.cpp), exist_ok=True)

    generate_hpp_from_json(args.json, args.hpp)
    generate_cpp_from_json(args.json, args.cpp)

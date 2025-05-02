import sys
from clang.cindex import Index, CursorKind, Config, TypeKind

desired_structs = [
    "D3DKMT",
    "D3DKMDT",
    "_D3DKMT",
    "_OUTPUTDUPL",
    "_DXGK",
    "_D3DGPU",
    "_DXGK",
    "tag",
    "_DXGKVGPU",
    "GPUP_",
    "D3DKMT_",
    "_D3DKMDT"
]

def parse_c_file(filename, clang_args=None):
    """
    Parse a C source file and return its TranslationUnit.
    :param filename: Path to the C file to parse.
    :param clang_args: List of extra arguments to pass to Clang (e.g. include paths, -D macros).
    """
    index = Index.create()
    args = clang_args if clang_args is not None else []
    tu = index.parse(filename, args=args)
    if not tu:
        raise RuntimeError(f"Unable to parse {filename}")
    return tu

def extract_structs_and_unions(tu_cursor):
    """
    Walk the AST and collect all top-level structs and unions.
    Returns a dict mapping name → { kind: 'struct'|'union', fields: [ (name, type_spelling) ] }.
    """
    results = {}

    for node in tu_cursor.get_children():
        if node.kind == CursorKind.STRUCT_DECL and node.is_definition():
            name = node.spelling or "<anonymous>"
            start_with = False
            for struct in desired_structs:
                if name.startswith(struct):
                    start_with = True
                    break
            if not start_with:
                continue
            kind = 'struct'
            fields = []
            # Iterate over field declarations inside
            for child in node.get_children():
                if child.kind == CursorKind.FIELD_DECL and not child.kind == CursorKind.UNION_DECL:
                    field_name = child.spelling
                    field_type = child.type.spelling
                    if not field_type.startswith('union') and not field_type.startswith('struct (unnamed '):
                        fields.append((field_name, field_type))
                    #print(f"Struct: {name}, Field: {field_name}, Type: {field_type}, Kind: {kind}")
            results[name] = {
                'kind': kind,
                'fields': fields
            }
    return results

def print_summary(structs_unions):
    """
    Nicely print the extracted structs and unions.
    """
    for name, info in structs_unions.items():
        print(f"{info['kind'].upper()} {name}")
        for fname, ftype in info['fields']:
            print(f"  - {ftype:20} {fname}")
        print()

def to_cpp(structs, cpp_path: str):
    with open(cpp_path, 'w', encoding='utf-8') as f:
        # Common headers
        f.write('#pragma once\n')
        f.write('#include <nlohmann/json.hpp>\n')
        f.write('#include <string>\n')
        f.write('#include <windows.h>\n')
        f.write('#include <bitset>\n')
        f.write('#include <d3dkmthk.h>  // for D3DKMT_* types\n\n')

        f.write('std::string VoidArrayToString(const void* arr, size_t size) {\n')
        f.write('    const auto* bytes = reinterpret_cast<const cct::UByte*>(arr);\n')
        f.write('    std::ostringstream oss;\n')
        f.write('    for (size_t i = 0; i < size; ++i) {\n')
        f.write('        std::bitset<8> bits(bytes[i]);\n')
        f.write('        oss << bits.to_string();\n')
        f.write('        if (i + 1 < size)\n')
        f.write("            oss << ' ';\n")
        f.write('    }\n')
        f.write('    return oss.str();\n')
        f.write('}\n\n')

        f.write('std::string LuidToString(const LUID& luid) {\n')
        f.write('    return std::format("LUID: {:08X}-{:08X}", luid.HighPart, luid.LowPart);\n')
        f.write('}\n\n')

        f.write('inline void to_json(nlohmann::json& j, const LARGE_INTEGER & obj) {\n')
        f.write('    j["LARGE_INTEGER"] = std::format("{}", obj.QuadPart);\n')
        f.write('}\n\n')

        f.write('inline void to_json(nlohmann::json& j, const void* obj) {\n')
        f.write('    j["HANDLE"] = std::format("{}", obj);\n')
        f.write('}\n\n')

        f.write('std::string GuidToString(const GUID& guid) {\n')
        f.write('    return std::format("{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}", guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);\n')
        f.write('}\n\n')

        for name, info in structs.items():
            f.write(f'inline void to_json(nlohmann::json& j, const {name}& obj);\n')
            f.write(f'inline void to_json(nlohmann::json& j, const {name}* obj) {{\n')
            f.write(f'    if (obj) {{\n')
            f.write(f'        to_json(j, *obj);\n')
            f.write(f'    }}\n')
            f.write(f'}}\n\n')
        f.write('\n')

        for struct_name, struct_data in structs.items():
            fields = struct_data.get('fields', [])
            f.write(f'inline void to_json(nlohmann::json& j, const {struct_name}& obj) {{\n')
            if struct_name in ['_D3DKMT_SETVIDPNSOURCEOWNER2', '_D3DKMT_WDDM_1_2_CAPS', '_OUTPUTDUPL_CONTEXT_DEBUG_INFO', '_DXGK_GRAPHICSPOWER_REGISTER_INPUT_V_1_2', '_DXGK_GRAPHICSPOWER_REGISTER_OUTPUT', '_DXGKARG_SETPALETTE', '_DXGK_DISPLAY_INFORMATION', '_D3DKMT_DISPLAYMODELIST', '_D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT3', '_D3DKMT_PRESENT_MULTIPLANE_OVERLAY3', 'D3DKMT_MIRACAST_CHUNK_DATA']:
                f.write('}\n\n')
                continue
            if struct_name.startswith("tag"):
                f.write('}\n\n')
                continue
            if struct_name.startswith("D3DKMT_") or struct_name.startswith("_D3DKMT") or struct_name.startswith("D3DKMDT"):
                for name, typ in fields:
                    if "Luid" in name or "LUID" in name or "luid" in name:
                        f.write(f'    j["{name}"] = LuidToString(*reinterpret_cast<const LUID*>(&obj.{name}));\n')  
                    elif "Guid" in name or "GUID" in name or "guid" in name:
                        f.write(f'    j["{name}"] = GuidToString(*reinterpret_cast<const GUID*>(&obj.{name}));\n')
                    elif name == "phAllocationList":
                        f.write(f'    j["{name}"] = VoidArrayToString(obj.{name}, obj.AllocationCount);\n')
                    elif  name == "pPrivateDriverData":
                        f.write(f'    j["{name}"] = VoidArrayToString(obj.{name}, obj.PrivateDriverDataSize);\n')
                    elif  name == "pPrivateRuntimeData":
                        f.write(f'    j["{name}"] = VoidArrayToString(obj.{name}, obj.PrivateRuntimeDataSize);\n')
                    elif name == "OutputDuplDebugInfos" or name == "AllocationCount" or name == "pEnumUIBuffer" or name == "SizeOfEnumUIBuffer" or name == "AllocationList" or name == "PriorityList" or name == "pPriorities" or name == "ChunkInfo":
                        pass
                    elif typ in ('D3DKMT_PTR_TYPE[8]', 'BOOL *', 'D3DKMT_PTR_TYPE *', 'D3DKMT_PTR_TYPE', 'const D3DKMT_PTR_TYPE*', 'const D3DDDI_VIDEO_PRESENT_SOURCE_ID *', 'const D3DKMT_VIDPNSOURCEOWNER_TYPE *', 'D3DKMT_QUERYSTATISTICS_RESULT', 'D3DKMT_PROCESS_VERIFIER_OPTION_DATA', 'D3DKMT_ADAPTER_VERIFIER_OPTION_DATA', 'D3DKMT_PROCESS_VERIFIER_OPTION', 'RECTL', 'D3DKMT_ENUMADAPTERS_FILTER', 'DISPLAYID_DETAILED_TIMING_TYPE_I', 'D3DKMDT_PALETTEDATA*', 'UINT *', 'const UINT *', 'const UINT64 *', 'UINT64', 'const D3DKMT_HANDLE *', 'PLARGE_INTEGER', 'const PFND3DKMT_TRIMNOTIFICATIONCALLBACK', 'const PFND3DKMT_TRIMNOTIFICATIONCALLBACK', 'const PFND3DKMT_BUDGETCHANGENOTIFICATIONCALLBACK', 'OBJECT_ATTRIBUTES *', 'D3DKMT_ALLOCATIONRESIDENCYSTATUS *', 'PFND3DKMT_BUDGETCHANGENOTIFICATIONCALLBACK', 'PFND3DKMT_TRIMNOTIFICATIONCALLBACK', 'PFND3DKMT_BUDGETCHANGENOTIFICATIONCALLBACK') or typ.startswith('D3DDDI'):
                        pass
                    elif typ in ('HMODULE', 'HANDLE', "HWND", "D3DKMT_HANDLE *", "HDC"):
                        f.write(f'    j["{name}"] = std::string(std::format("{{}}", static_cast<void*>(obj.{name})));\n')
                    elif typ == 'const void *' or typ == 'void *' or typ == 'PVOID' or typ == 'LPVOID':
                        f.write(f'    j["{name}"] = std::format("{{}}", obj.{name});\n')
                    elif name == "Flags":
                        f.write(f'    j["{name}"] = *reinterpret_cast<const UINT*>(&obj.{name});\n')
                    else:
                        f.write(f'    j["{name}"] = obj.{name};\n')
            f.write('}\n\n')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Parse C structs to JSON')
    parser.add_argument('d3dukmdt', help='Path to d3dukmdt.h')
    parser.add_argument('d3dkmthk', help='Path to d3dkmthk.h')
    parser.add_argument('d3dkmdt', help='Path to d3dkmdt.h')
    parser.add_argument('output', help='Path to output hpp')
    args = parser.parse_args()
    
    c_file = '#include <Windows.h>\n#include <d3dkmthk.h>\n#include <d3dukmdt.h>\n#include <d3dkmthk.h>'
    
    with open("./fake.c", 'w') as f:
        f.write(c_file)
        
    tu = parse_c_file("./fake.c", None)
    json_struct = extract_structs_and_unions(tu.cursor)

    to_cpp(json_struct, args.output)

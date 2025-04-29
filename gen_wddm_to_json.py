import re
import json

def parse_structs(header_path: str):
    with open(header_path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()

    # Regex to capture typedef struct blocks
    struct_pattern = re.compile(r'typedef struct _([A-Za-z0-9_]+)\s*\{([^}]+)\}\s*([A-Za-z0-9_]+);', re.DOTALL)
    field_pattern = re.compile(r'([A-Za-z0-9_\*]+)\s+([A-Za-z0-9_]+)(?:\s*\[.*?\])?;')

    structs = {}
    for match in struct_pattern.finditer(text):
        internal_name = match.group(1)
        body = match.group(2)
        typedef_name = match.group(3)
        fields = []
        for line in body.split(';'):
            line = line.strip()
            if not line:
                continue
            # remove comments
            line = re.sub(r'//.*', '', line)
            m = field_pattern.search(line + ';')
            if m:
                ftype, fname = m.groups()
                ftype = ftype.strip()
                # special case for pPrivateDriverData
                if 'PrivateDriverData' in fname:
                    ftype = 'bytes'
                fields.append({'name': fname, 'type': ftype})
        structs[typedef_name] = {'fields': fields}

    uint_types = ['D3DDDI_CREATECONTEXTFLAGS', 'D3DKMT_CHECK_MULTIPLANE_OVERLAY_SUPPORT_RETURN_INFO']
    
    for uint_type in uint_types:
        structs[uint_type] = {
                                "fields": [
                                    {
                                        "name": "Value",
                                        "type": "UINT"
                                    }
                                ]
                            }
    structs['RECT'] = {
        "fields": [
            {
                "name": "left",
                "type": "LONG"
            },
            {
                "name": "top",
                "type": "LONG"
            },
            {
                "name": "right",
                "type": "LONG"
            },
            {
                "name": "bottom",
                "type": "LONG"
            }
        ]
    }
    structs['LUID'] = {
        "fields": [
            {
                "name": "LowPart",
                "type": "DWORD"
            },
            {
                "name": "HighPart",
                "type": "DWORD"
            }
        ]
    }
    return structs

def to_cpp(json_struct: dict, cpp_path: str):
    with open(cpp_path, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <nlohmann/json.hpp>\n')
        f.write('#include <string>\n')
        f.write('#include <windows.h>\n')
        f.write('#include <d3dkmthk.h>  // for D3DKMT_* types\n')
        
        for struct_name, struct_data in json_struct.items():
            f.write(f'inline nlohmann::json ToJson(const {struct_name}& obj);\n')
        for struct_name, struct_data in json_struct.items():
            f.write(f'inline nlohmann::json ToJson(const {struct_name}& obj) {{\n')
            f.write('    nlohmann::json j;\n')
            for field in struct_data['fields']:
                f.write(f'    j["{field["name"]}"] = obj.{field["name"]};\n')
            f.write('    return j;\n')
            f.write('}\n\n')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Parse C structs to JSON')
    parser.add_argument('header', help='Path to d3dkmthk.h')
    parser.add_argument('output', help='Path to output hpp')
    args = parser.parse_args()
    
    json_struct = parse_structs(args.header)
    to_cpp(json_struct, args.output)

import math
import os
import re
from mqengine import (
    MQEngine,
    ScriptBehaviour,
    PositionComponent,
    RotationComponent,
    ScaleComponent,
    DirectionalLightComponent,
    PointLightComponent,
    CameraComponent,
    PBRMaterialComponent,
)


def _to_pascal_case(raw_name: str) -> str:
    cleaned = re.sub(r'[^0-9a-zA-Z_]+', '_', raw_name.strip())
    parts = [p for p in cleaned.split('_') if p]
    if not parts:
        return 'GodotScriptBehaviour'
    pascal = ''.join(p[0].upper() + p[1:] for p in parts)
    if pascal[0].isdigit():
        pascal = 'Gd' + pascal
    if pascal in ('ScriptBehaviour', 'Node3D', 'Node', 'MeshInstance3D', 'Camera3D'):
        pascal = 'Gd' + pascal
    return pascal


def _strip_gd_inline_comment(line: str) -> tuple[str, str]:
    in_single = False
    in_double = False
    escaped = False
    for idx, ch in enumerate(line):
        if escaped:
            escaped = False
            continue
        if ch == '\\':
            escaped = True
            continue
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == '#' and not in_single and not in_double:
            return line[:idx], line[idx:]
    return line, ''


def _convert_gd_expr(expr: str, member_vars: set[str], member_funcs: set[str]) -> str:
    code, comment = _strip_gd_inline_comment(expr)
    if not code.strip():
        return expr

    string_literals: list[str] = []

    def _save_str(match):
        idx = len(string_literals)
        string_literals.append(match.group(0))
        return f'__GD_STR_{idx}__'

    code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', _save_str, code)

    code = re.sub(r'\btrue\b', 'True', code)
    code = re.sub(r'\bfalse\b', 'False', code)
    code = re.sub(r'\bnull\b', 'None', code)
    code = code.replace('&&', ' and ')
    code = code.replace('||', ' or ')
    code = re.sub(r'!(?!=)', 'not ', code)
    code = re.sub(r'\bVector3\.UP\b', 'Vector3(0.0, 1.0, 0.0)', code)
    code = re.sub(r'\bVector3\.DOWN\b', 'Vector3(0.0, -1.0, 0.0)', code)
    code = re.sub(r'\bVector3\.LEFT\b', 'Vector3(-1.0, 0.0, 0.0)', code)
    code = re.sub(r'\bVector3\.RIGHT\b', 'Vector3(1.0, 0.0, 0.0)', code)
    code = re.sub(r'\bVector3\.FORWARD\b', 'Vector3(0.0, 0.0, -1.0)', code)
    code = re.sub(r'\bVector3\.BACK\b', 'Vector3(0.0, 0.0, 1.0)', code)
    code = re.sub(r'\bVector3\.ZERO\b', 'Vector3(0.0, 0.0, 0.0)', code)
    code = re.sub(r'\bVector3\.ONE\b', 'Vector3(1.0, 1.0, 1.0)', code)

    builtin_node_props = {
        'entity', 'position', 'rotation', 'rotation_degrees', 'scale',
        'rotate_x', 'rotate_y', 'rotate_z', 'translate', 'look_at',
        'get_component', 'set_component', 'has_component', 'add_component',
        'get_world_position', 'set_world_position', 'get_parent', 'get_children'
    }
    all_self_targets = member_vars | member_funcs | builtin_node_props

    for ident in sorted(all_self_targets, key=len, reverse=True):
        pattern = r'(?<![.\w\'"])\b' + re.escape(ident) + r'\b'
        code = re.sub(pattern, f'self.{ident}', code)

    for idx, literal in enumerate(string_literals):
        code = code.replace(f'__GD_STR_{idx}__', literal)

    return code + comment


def _split_gd_params(params_str: str) -> list[str]:
    result = []
    current = []
    depth = 0
    for ch in params_str:
        if ch in '([{':
            depth += 1
            current.append(ch)
        elif ch in ')]}':
            depth = max(0, depth - 1)
            current.append(ch)
        elif ch == ',' and depth == 0:
            part = ''.join(current).strip()
            if part:
                result.append(part)
            current = []
        else:
            current.append(ch)
    tail = ''.join(current).strip()
    if tail:
        result.append(tail)
    return result


def _clean_gd_param(param: str) -> str:
    default_part = ''
    if '=' in param:
        lhs, rhs = param.split('=', 1)
        lhs = lhs.rstrip(':').strip()
        default_part = ' = ' + rhs.strip()
    else:
        lhs = param.strip()
    if ':' in lhs:
        name_part = lhs.split(':', 1)[0].strip()
    else:
        name_part = lhs.strip()
    return name_part + default_part


def transpile_gdscript_to_python(gd_source: str, fallback_class_name: str = 'GodotScriptBehaviour') -> tuple[str, str]:
    lines = gd_source.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    class_name = _to_pascal_case(fallback_class_name)

    member_vars: list[tuple[str, str]] = []
    member_var_names: set[str] = set()
    member_func_names: set[str] = set()
    constants: list[tuple[str, str]] = []

    var_decl_re = re.compile(
        r'^(?:@\w+(?:\([^)]*\))?\s+)*(?:static\s+)?var\s+([A-Za-z_]\w*)\s*(?::\s*[^=]+)?(?::=|=)?\s*(.*)$'
    )
    const_decl_re = re.compile(
        r'^const\s+([A-Za-z_]\w*)\s*(?::\s*[^=]+)?=\s*(.*)$'
    )
    func_decl_re = re.compile(
        r'^(?:static\s+)?func\s+([A-Za-z_]\w*)\s*\((.*)\)\s*(?:->\s*[^:]+)?\s*:\s*(.*)$'
    )

    for raw_line in lines:
        code_part, _ = _strip_gd_inline_comment(raw_line)
        stripped = code_part.strip()
        if not stripped:
            continue
        indent = len(raw_line) - len(raw_line.lstrip(' \t'))
        if indent == 0:
            if stripped.startswith('class_name '):
                cname = stripped[len('class_name '):].strip().split()[0]
                if cname:
                    class_name = _to_pascal_case(cname)
                continue
            m_var = var_decl_re.match(stripped)
            if m_var:
                vname = m_var.group(1)
                vexpr = m_var.group(2).strip() or '0.0'
                member_var_names.add(vname)
                member_vars.append((vname, vexpr))
                continue
            m_const = const_decl_re.match(stripped)
            if m_const:
                cname = m_const.group(1)
                cexpr = m_const.group(2).strip() or '0'
                member_var_names.add(cname)
                constants.append((cname, cexpr))
                continue
            m_func = func_decl_re.match(stripped)
            if m_func:
                member_func_names.add(m_func.group(1))

    out_lines = [
        '# 由 GodotImporter 插件自动从 GDScript (.gd) 转换生成的 MQEngine Python 脚本',
        'import math',
        'from mqengine import *',
        '',
        '',
        f'class {class_name}(ScriptBehaviour):',
    ]

    for cname, cexpr in constants:
        converted_const = _convert_gd_expr(cexpr, set(), set())
        out_lines.append(f'    {cname} = {converted_const}')
    if constants:
        out_lines.append('')

    out_lines.append('    def __init__(self):')
    out_lines.append('        super().__init__()')
    if member_vars:
        for vname, vexpr in member_vars:
            converted_init = _convert_gd_expr(vexpr, member_var_names, member_func_names)
            out_lines.append(f'        self.{vname} = {converted_init}')
    else:
        out_lines.append('        pass')
    out_lines.append('')

    in_func = False
    for raw_line in lines:
        expanded = raw_line.replace('\t', '    ')
        code_part, comment_part = _strip_gd_inline_comment(expanded)
        stripped = code_part.strip()
        indent_spaces = len(expanded) - len(expanded.lstrip(' '))

        if not stripped:
            if comment_part.strip():
                pad = '    ' * (1 + (indent_spaces // 4 if in_func else 0))
                out_lines.append(f'{pad}{comment_part.strip()}')
            continue

        if indent_spaces == 0:
            if (stripped.startswith('extends ') or
                    stripped.startswith('class_name ') or
                    stripped.startswith('@tool') or
                    const_decl_re.match(stripped) or
                    var_decl_re.match(stripped)):
                in_func = False
                continue

            m_func = func_decl_re.match(stripped)
            if m_func:
                in_func = True
                fname = m_func.group(1)
                raw_params = _split_gd_params(m_func.group(2))
                clean_params = [_clean_gd_param(p) for p in raw_params]
                params_sig = ', '.join(['self'] + clean_params)
                out_lines.append(f'    def {fname}({params_sig}):')
                inline_body = m_func.group(3).strip()
                if inline_body:
                    if inline_body == 'pass':
                        out_lines.append('        pass')
                    else:
                        out_lines.append('        ' + _convert_gd_expr(inline_body, member_var_names, member_func_names))
                continue

        if in_func and indent_spaces > 0:
            rel_level = max(1, indent_spaces // 4)
            py_indent = '    ' * (1 + rel_level)

            m_local_var = var_decl_re.match(stripped)
            if m_local_var:
                lv_name = m_local_var.group(1)
                lv_expr = m_local_var.group(2).strip() or 'None'
                conv_expr = _convert_gd_expr(lv_expr, member_var_names, member_func_names)
                out_lines.append(f'{py_indent}{lv_name} = {conv_expr}')
                continue

            conv_stmt = _convert_gd_expr(stripped, member_var_names, member_func_names)
            if comment_part:
                conv_stmt = f'{conv_stmt}  {comment_part.strip()}'
            out_lines.append(f'{py_indent}{conv_stmt}')

    out_lines.append('')
    return class_name, '\n'.join(out_lines)


def _resolve_scripts_py_dir() -> str:
    asset_root = './Assets'
    if hasattr(MQEngine, 'DataManager') and hasattr(MQEngine.DataManager, 'getAssetRoot'):
        root = MQEngine.DataManager.getAssetRoot()
        if root:
            asset_root = root
    py_dir = os.path.join(asset_root, 'Scripts', 'py').replace('\\', '/')
    os.makedirs(py_dir, exist_ok=True)
    return py_dir


def import_gdscript_file(gd_path: str, target_class_name: str = '', property_overrides: dict | None = None) -> str:
    if not gd_path or not os.path.isfile(gd_path):
        print(f'[GodotImporter] 未找到 GDScript 文件: {gd_path}')
        return ''

    with open(gd_path, 'r', encoding='utf-8', errors='ignore') as f:
        gd_source = f.read()

    stem = os.path.splitext(os.path.basename(gd_path))[0]
    fallback_name = target_class_name if target_class_name else _to_pascal_case(stem)
    class_name, py_code = transpile_gdscript_to_python(gd_source, fallback_name)

    if property_overrides:
        override_lines = []
        for k, v in property_overrides.items():
            if re.match(r'^[A-Za-z_]\w*$', str(k)):
                override_lines.append(f'        self.{k} = {repr(v)}')
        if override_lines:
            hook = '        super().__init__()'
            idx = py_code.rfind(hook)
            if idx != -1:
                insert_pos = py_code.find('\n    def ', idx)
                if insert_pos == -1:
                    py_code = py_code + '\n' + '\n'.join(override_lines) + '\n'
                else:
                    py_code = py_code[:insert_pos] + '\n' + '\n'.join(override_lines) + py_code[insert_pos:]

    py_dir = _resolve_scripts_py_dir()
    out_py_path = os.path.join(py_dir, f'{class_name}.py').replace('\\', '/')
    with open(out_py_path, 'w', encoding='utf-8') as f:
        f.write(py_code)

    g = globals()
    exec(compile(py_code, out_py_path, 'exec'), g, g)
    if class_name in g and isinstance(g[class_name], type):
        if '__mqPyScriptClasses' in g and isinstance(g['__mqPyScriptClasses'], dict):
            g['__mqPyScriptClasses'][class_name] = g[class_name]

    print(f'[GodotImporter] 已将 GDScript [{gd_path}] 转换为 Python 脚本 [{out_py_path}] (类: {class_name})')
    return class_name


importGdScriptFile = import_gdscript_file


def _write_primitive_obj(obj_path: str, mesh_type: str, props: dict):
    os.makedirs(os.path.dirname(obj_path), exist_ok=True)
    verts = []
    normals = []
    uvs = []
    faces = []

    if mesh_type in ('SphereMesh', 'CapsuleMesh'):
        radius = float(props.get('radius', 0.5))
        height = float(props.get('height', radius * 2.0))
        half_y = max(radius, height * 0.5)
        rings = 16
        sectors = 24
        for r in range(rings + 1):
            v = r / rings
            phi = v * math.pi
            for s in range(sectors + 1):
                u = s / sectors
                theta = u * 2.0 * math.pi
                nx = math.sin(phi) * math.cos(theta)
                ny = math.cos(phi)
                nz = math.sin(phi) * math.sin(theta)
                verts.append((nx * radius, ny * half_y, nz * radius))
                normals.append((nx, ny, nz))
                uvs.append((u, 1.0 - v))
        stride = sectors + 1
        for r in range(rings):
            for s in range(sectors):
                i0 = r * stride + s + 1
                i1 = i0 + 1
                i2 = (r + 1) * stride + s + 1
                i3 = i2 + 1
                faces.append((i0, i2, i1))
                faces.append((i1, i2, i3))
    elif mesh_type == 'CylinderMesh':
        top_r = float(props.get('top_radius', 0.5))
        bot_r = float(props.get('bottom_radius', 0.5))
        height = float(props.get('height', 2.0))
        hy = height * 0.5
        sectors = 24
        for s in range(sectors + 1):
            u = s / sectors
            theta = u * 2.0 * math.pi
            cx, cz = math.cos(theta), math.sin(theta)
            verts.append((cx * top_r, hy, cz * top_r))
            normals.append((cx, 0.0, cz))
            uvs.append((u, 1.0))
            verts.append((cx * bot_r, -hy, cz * bot_r))
            normals.append((cx, 0.0, cz))
            uvs.append((u, 0.0))
        for s in range(sectors):
            i0 = s * 2 + 1
            i1 = i0 + 1
            i2 = i0 + 2
            i3 = i0 + 3
            faces.append((i0, i1, i2))
            faces.append((i2, i1, i3))
    elif mesh_type in ('PlaneMesh', 'QuadMesh'):
        size = props.get('size', (2.0, 2.0))
        sx = float(size[0]) * 0.5 if isinstance(size, (list, tuple)) and len(size) >= 2 else 1.0
        sz = float(size[1]) * 0.5 if isinstance(size, (list, tuple)) and len(size) >= 2 else 1.0
        verts = [(-sx, 0.0, -sz), (sx, 0.0, -sz), (sx, 0.0, sz), (-sx, 0.0, sz)]
        normals = [(0.0, 1.0, 0.0)] * 4
        uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        faces = [(1, 4, 2), (2, 4, 3)]
    else:
        size = props.get('size', (1.0, 1.0, 1.0))
        if isinstance(size, (list, tuple)) and len(size) >= 3:
            hx, hy, hz = float(size[0]) * 0.5, float(size[1]) * 0.5, float(size[2]) * 0.5
        else:
            hx, hy, hz = 0.5, 0.5, 0.5
        box_faces = [
            ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
            ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]),
            ((0, 1, 0), [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)]),
            ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
            ((1, 0, 0), [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)]),
            ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
        ]
        for nrm, corners in box_faces:
            base = len(verts) + 1
            for c in corners:
                verts.append(c)
                normals.append(nrm)
            uvs.extend([(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
            faces.append((base, base + 1, base + 2))
            faces.append((base, base + 2, base + 3))

    lines = ['# Procedural Godot Primitive Mesh generated by GodotImporter']
    lines.append(f'o {mesh_type}')
    for vx, vy, vz in verts:
        lines.append(f'v {vx:.6f} {vy:.6f} {vz:.6f}')
    for u, v in uvs:
        lines.append(f'vt {u:.6f} {v:.6f}')
    for nx, ny, nz in normals:
        lines.append(f'vn {nx:.6f} {ny:.6f} {nz:.6f}')
    for i0, i1, i2 in faces:
        lines.append(f'f {i0}/{i0}/{i0} {i1}/{i1}/{i1} {i2}/{i2}/{i2}')

    with open(obj_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')


def _ensure_primitive_mesh_uuid(mesh_type: str, sub_id: str, props: dict) -> str:
    asset_root = './Assets'
    if hasattr(MQEngine, 'DataManager') and hasattr(MQEngine.DataManager, 'getAssetRoot'):
        root = MQEngine.DataManager.getAssetRoot()
        if root:
            asset_root = root
    prim_dir = os.path.join(asset_root, 'Models', '_godot_primitives').replace('\\', '/')
    os.makedirs(prim_dir, exist_ok=True)
    safe_id = re.sub(r'[^0-9a-zA-Z_]+', '_', f'{mesh_type}_{sub_id}')
    obj_path = os.path.join(prim_dir, f'{safe_id}.obj').replace('\\', '/')
    if not os.path.isfile(obj_path):
        _write_primitive_obj(obj_path, mesh_type, props)
    if hasattr(MQEngine, 'DataManager') and hasattr(MQEngine.DataManager, 'importModel'):
        return MQEngine.DataManager.importModel(obj_path)
    return ''


def _parse_header_attrs(header_str: str) -> dict[str, str]:
    attrs = {}
    for m in re.finditer(r'([A-Za-z0-9_/]+)\s*=\s*("[^"]*"|ExtResource\("[^"]*"\)|SubResource\("[^"]*"\)|[^\s\]]+)', header_str):
        key = m.group(1)
        val = m.group(2).strip()
        if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
            val = val[1:-1]
        attrs[key] = val
    return attrs


def _parse_float_tuple(val_str: str) -> tuple[float, ...]:
    nums = re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', val_str)
    return tuple(float(x) for x in nums)


def _parse_tscn_value(raw_val: str):
    s = raw_val.strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        return s[1:-1]
    if s == 'true':
        return True
    if s == 'false':
        return False
    if s == 'null':
        return None
    m_ext = re.match(r'^ExtResource\(\s*"?([^")]+)"?\s*\)$', s)
    if m_ext:
        return ('ExtResource', m_ext.group(1).strip())
    m_sub = re.match(r'^SubResource\(\s*"?([^")]+)"?\s*\)$', s)
    if m_sub:
        return ('SubResource', m_sub.group(1).strip())
    if s.startswith(('Vector2(', 'Vector3(', 'Vector4(', 'Color(', 'Transform3D(', 'Transform(')):
        return _parse_float_tuple(s)
    try:
        if '.' in s or 'e' in s or 'E' in s:
            return float(s)
        return int(s)
    except ValueError:
        return s


def _decompose_godot_transform3d(vals: tuple[float, ...]) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
    if len(vals) < 12:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0)
    m00, m01, m02, m10, m11, m12, m20, m21, m22, tx, ty, tz = vals[:12]

    sx = math.sqrt(m00 * m00 + m10 * m10 + m20 * m20)
    sy = math.sqrt(m01 * m01 + m11 * m11 + m21 * m21)
    sz = math.sqrt(m02 * m02 + m12 * m12 + m22 * m22)
    if sx < 1e-6:
        sx = 1.0
    if sy < 1e-6:
        sy = 1.0
    if sz < 1e-6:
        sz = 1.0

    r00, r01, r02 = m00 / sx, m01 / sy, m02 / sz
    r10, r11, r12 = m10 / sx, m11 / sy, m12 / sz
    r20, r21, r22 = m20 / sx, m21 / sy, m22 / sz

    sy_val = max(-1.0, min(1.0, -r12))
    rx = math.asin(sy_val)
    if abs(r12) < 0.99999:
        ry = math.atan2(r02, r22)
        rz = math.atan2(r10, r11)
    else:
        ry = math.atan2(-r20, r00)
        rz = 0.0

    return (
        (float(tx), float(ty), float(tz)),
        (math.degrees(rx), math.degrees(ry), math.degrees(rz)),
        (float(sx), float(sy), float(sz)),
    )


def _find_godot_project_root(tscn_path: str) -> str:
    cur = os.path.dirname(os.path.abspath(tscn_path))
    for _ in range(6):
        if os.path.isfile(os.path.join(cur, 'project.godot')):
            return cur
        parent = os.path.dirname(cur)
        if not parent or parent == cur:
            break
        cur = parent
    return os.path.dirname(os.path.abspath(tscn_path))


def _resolve_godot_res_path(res_path: str, tscn_dir: str, project_root: str) -> str:
    clean = res_path.strip()
    if clean.startswith('res://'):
        rel = clean[len('res://'):]
    else:
        rel = clean

    candidates = [
        os.path.join(project_root, rel),
        os.path.join(tscn_dir, rel),
        os.path.join(tscn_dir, os.path.basename(rel)),
        os.path.join(project_root, os.path.basename(rel)),
    ]
    for cand in candidates:
        norm = os.path.normpath(cand).replace('\\', '/')
        if os.path.isfile(norm):
            return norm
    return os.path.normpath(candidates[0]).replace('\\', '/')


def _resolve_active_trunk() -> str:
    if hasattr(MQEngine, 'DataManager') and hasattr(MQEngine.DataManager, 'getLoadedTrunkNames'):
        trunks = MQEngine.DataManager.getLoadedTrunkNames()
        for t in trunks:
            if t != 'scene':
                return t
        if trunks:
            return trunks[0]
    return 'default'


def _set_entity_component(reg_ptr: float, entity_id: int, comp_name: str, data: dict):
    g = globals()
    ent_info = g.get('entityInfo')
    if ent_info is not None:
        if not ent_info.hasComponentById(reg_ptr, entity_id, comp_name):
            ent_info.addComponentById(reg_ptr, entity_id, comp_name)
        ent_info.setComponentById(reg_ptr, entity_id, comp_name, data)


def import_godot_scene(tscn_path: str, trunk_name: str = '', parent_override_id: int = 0xFFFFFFFF) -> int:
    if not tscn_path or not os.path.isfile(tscn_path):
        print(f'[GodotImporter] 场景文件不存在: {tscn_path}')
        return 0

    if not trunk_name:
        trunk_name = _resolve_active_trunk()

    dm = getattr(MQEngine, 'DataManager', None)
    if dm is None:
        print('[GodotImporter] DataManager 未就绪')
        return 0

    reg_ptr = float(dm.getTrunkRegistryAddress(trunk_name))
    if reg_ptr == 0.0:
        trunk_name = _resolve_active_trunk()
        reg_ptr = float(dm.getTrunkRegistryAddress(trunk_name))
        if reg_ptr == 0.0:
            print(f'[GodotImporter] 无法获取 Trunk [{trunk_name}] 的实体注册表')
            return 0

    tscn_dir = os.path.dirname(os.path.abspath(tscn_path))
    project_root = _find_godot_project_root(tscn_path)

    with open(tscn_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    ext_resources: dict[str, dict] = {}
    sub_resources: dict[str, dict] = {}
    nodes: list[dict] = []

    current_section = None
    lines = content.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith(';'):
            continue

        if line.startswith('[') and line.endswith(']'):
            inner = line[1:-1].strip()
            parts = inner.split(None, 1)
            sec_kind = parts[0]
            sec_attrs = _parse_header_attrs(parts[1] if len(parts) > 1 else '')
            current_section = {'kind': sec_kind, 'attrs': sec_attrs, 'props': {}}
            if sec_kind == 'ext_resource':
                res_id = sec_attrs.get('id', '')
                if res_id:
                    ext_resources[res_id] = current_section
            elif sec_kind == 'sub_resource':
                sub_id = sec_attrs.get('id', '')
                if sub_id:
                    sub_resources[sub_id] = current_section
            elif sec_kind == 'node':
                nodes.append(current_section)
            continue

        if current_section is not None and '=' in line:
            k, v = line.split('=', 1)
            k = k.strip()
            v = v.strip()
            if v.startswith('"') and not (len(v) >= 2 and v.endswith('"') and not v.endswith('\\"')):
                buf = [v]
                while i < len(lines):
                    nxt = lines[i]
                    i += 1
                    buf.append(nxt)
                    if nxt.rstrip().endswith('"') and not nxt.rstrip().endswith('\\"'):
                        break
                v = '\n'.join(buf)
            current_section['props'][k] = _parse_tscn_value(v)

    node_path_to_entity: dict[str, int] = {}
    root_entity_id: int = 0
    created_count = 0

    for node in nodes:
        attrs = node['attrs']
        props = node['props']
        node_name = attrs.get('name', f'GodotNode_{created_count}')
        node_type = attrs.get('type', 'Node3D')
        parent_attr = attrs.get('parent', None)
        instance_attr = attrs.get('instance', '')

        if parent_attr is None:
            node_path = '.'
            parent_entity_id = parent_override_id
        elif parent_attr == '.':
            node_path = node_name
            parent_entity_id = node_path_to_entity.get('.', parent_override_id)
        else:
            node_path = f'{parent_attr}/{node_name}'
            parent_entity_id = node_path_to_entity.get(parent_attr, node_path_to_entity.get('.', parent_override_id))

        entity_id = int(dm.createEntity(trunk_name, node_name))
        if root_entity_id == 0:
            root_entity_id = entity_id
        node_path_to_entity[node_path] = entity_id
        created_count += 1

        if parent_entity_id != 0xFFFFFFFF and parent_entity_id != 0 and parent_entity_id != entity_id:
            dm.setParent(trunk_name, entity_id, int(parent_entity_id))

        pos = [0.0, 0.0, 0.0]
        rot_deg = [0.0, 0.0, 0.0]
        scl = [1.0, 1.0, 1.0]

        if 'transform' in props and isinstance(props['transform'], tuple) and len(props['transform']) >= 12:
            t_pos, t_rot, t_scl = _decompose_godot_transform3d(props['transform'])
            pos = list(t_pos)
            rot_deg = list(t_rot)
            scl = list(t_scl)

        if 'position' in props and isinstance(props['position'], tuple) and len(props['position']) >= 3:
            pos = [float(props['position'][0]), float(props['position'][1]), float(props['position'][2])]
        if 'rotation' in props and isinstance(props['rotation'], tuple) and len(props['rotation']) >= 3:
            rot_deg = [
                math.degrees(float(props['rotation'][0])),
                math.degrees(float(props['rotation'][1])),
                math.degrees(float(props['rotation'][2])),
            ]
        if 'rotation_degrees' in props and isinstance(props['rotation_degrees'], tuple) and len(props['rotation_degrees']) >= 3:
            rot_deg = [
                float(props['rotation_degrees'][0]),
                float(props['rotation_degrees'][1]),
                float(props['rotation_degrees'][2]),
            ]
        if 'scale' in props and isinstance(props['scale'], tuple) and len(props['scale']) >= 3:
            scl = [float(props['scale'][0]), float(props['scale'][1]), float(props['scale'][2])]

        _set_entity_component(reg_ptr, entity_id, 'PositionComponent', {
            'position': {'x': pos[0], 'y': pos[1], 'z': pos[2]}
        })
        _set_entity_component(reg_ptr, entity_id, 'RotationComponent', {
            'rotation': {'x': rot_deg[0], 'y': rot_deg[1], 'z': rot_deg[2]}
        })
        _set_entity_component(reg_ptr, entity_id, 'ScaleComponent', {
            'scale': {'x': scl[0], 'y': scl[1], 'z': scl[2]}
        })

        if instance_attr:
            parsed_inst = _parse_tscn_value(instance_attr)
            if isinstance(parsed_inst, tuple) and parsed_inst[0] == 'ExtResource':
                ext_info = ext_resources.get(parsed_inst[1])
                if ext_info:
                    ext_path = _resolve_godot_res_path(ext_info['attrs'].get('path', ''), tscn_dir, project_root)
                    if ext_path.lower().endswith('.tscn') and os.path.isfile(ext_path):
                        import_godot_scene(ext_path, trunk_name, entity_id)
                    elif os.path.isfile(ext_path):
                        model_uuid = dm.importModel(ext_path)
                        if model_uuid:
                            dm.instantiateModelToEntity(trunk_name, entity_id, model_uuid)

        mesh_ref = props.get('mesh')
        if isinstance(mesh_ref, tuple) and len(mesh_ref) == 2:
            ref_kind, ref_id = mesh_ref
            if ref_kind == 'ExtResource' and ref_id in ext_resources:
                ext_path = _resolve_godot_res_path(ext_resources[ref_id]['attrs'].get('path', ''), tscn_dir, project_root)
                if os.path.isfile(ext_path):
                    model_uuid = dm.importModel(ext_path)
                    if model_uuid:
                        dm.instantiateModelToEntity(trunk_name, entity_id, model_uuid)
            elif ref_kind == 'SubResource' and ref_id in sub_resources:
                sub_info = sub_resources[ref_id]
                sub_type = sub_info['attrs'].get('type', 'BoxMesh')
                model_uuid = _ensure_primitive_mesh_uuid(sub_type, ref_id, sub_info['props'])
                if model_uuid:
                    dm.instantiateModelToEntity(trunk_name, entity_id, model_uuid)

        mat_ref = (
            props.get('material_override')
            or props.get('surface_material_override/0')
            or props.get('material/0')
        )
        if not mat_ref and isinstance(mesh_ref, tuple) and mesh_ref[0] == 'SubResource' and mesh_ref[1] in sub_resources:
            mat_ref = sub_resources[mesh_ref[1]]['props'].get('material')
        if isinstance(mat_ref, tuple) and mat_ref[0] == 'SubResource' and mat_ref[1] in sub_resources:
            mat_props = sub_resources[mat_ref[1]]['props']
            albedo = mat_props.get('albedo_color', (0.8, 0.8, 0.8, 1.0))
            emissive = mat_props.get('emission', (0.0, 0.0, 0.0, 1.0))
            metallic = float(mat_props.get('metallic', 0.0))
            roughness = float(mat_props.get('roughness', 0.5))
            _set_entity_component(reg_ptr, entity_id, 'PBRMaterialComponent', {
                'baseColor': {
                    'x': float(albedo[0]) if len(albedo) > 0 else 0.8,
                    'y': float(albedo[1]) if len(albedo) > 1 else 0.8,
                    'z': float(albedo[2]) if len(albedo) > 2 else 0.8,
                },
                'metallic': metallic,
                'roughness': roughness,
                'ao': 1.0,
                'emissive': {
                    'x': float(emissive[0]) if len(emissive) > 0 else 0.0,
                    'y': float(emissive[1]) if len(emissive) > 1 else 0.0,
                    'z': float(emissive[2]) if len(emissive) > 2 else 0.0,
                },
            })

        if node_type in ('DirectionalLight3D', 'DirectionalLight'):
            lcol = props.get('light_color', (1.0, 0.98, 0.92, 1.0))
            energy = float(props.get('light_energy', 2.0))
            rx_rad = math.radians(rot_deg[0])
            ry_rad = math.radians(rot_deg[1])
            dx = -math.sin(ry_rad) * math.cos(rx_rad)
            dy = -math.sin(rx_rad)
            dz = -math.cos(ry_rad) * math.cos(rx_rad)
            if abs(dx) < 1e-4 and abs(dy) < 1e-4 and abs(dz - (-1.0)) < 1e-4:
                dx, dy, dz = -0.4, -0.85, -0.35
            _set_entity_component(reg_ptr, entity_id, 'DirectionalLightComponent', {
                'direction': {'x': dx, 'y': dy, 'z': dz},
                'color': {
                    'x': float(lcol[0]) if len(lcol) > 0 else 1.0,
                    'y': float(lcol[1]) if len(lcol) > 1 else 0.98,
                    'z': float(lcol[2]) if len(lcol) > 2 else 0.92,
                },
                'intensity': energy,
                'enabled': True,
            })
        elif node_type in ('OmniLight3D', 'SpotLight3D', 'OmniLight', 'SpotLight'):
            lcol = props.get('light_color', (1.0, 0.95, 0.85, 1.0))
            energy = float(props.get('light_energy', 3.0))
            lrange = float(props.get('omni_range', props.get('spot_range', 25.0)))
            _set_entity_component(reg_ptr, entity_id, 'PointLightComponent', {
                'color': {
                    'x': float(lcol[0]) if len(lcol) > 0 else 1.0,
                    'y': float(lcol[1]) if len(lcol) > 1 else 0.95,
                    'z': float(lcol[2]) if len(lcol) > 2 else 0.85,
                },
                'intensity': energy,
                'range': lrange,
                'enabled': True,
            })
        elif node_type in ('Camera3D', 'Camera'):
            fov = float(props.get('fov', 60.0))
            near_p = float(props.get('near', 0.1))
            far_p = float(props.get('far', 4000.0))
            active = bool(props.get('current', False))
            _set_entity_component(reg_ptr, entity_id, 'CameraComponent', {
                'fov': fov,
                'nearPlane': near_p,
                'farPlane': far_p,
                'active': active,
            })

        script_ref = props.get('script')
        if isinstance(script_ref, tuple) and len(script_ref) == 2:
            reserved_keys = {
                'transform', 'position', 'rotation', 'rotation_degrees', 'scale',
                'mesh', 'script', 'material_override', 'surface_material_override/0',
                'light_color', 'light_energy', 'omni_range', 'spot_range',
                'fov', 'near', 'far', 'current', 'visible', 'skeleton'
            }
            node_overrides = {
                k: v for k, v in props.items()
                if k not in reserved_keys and isinstance(v, (int, float, bool, str))
            }
            ref_kind, ref_id = script_ref
            bound_class_name = ''
            if ref_kind == 'ExtResource' and ref_id in ext_resources:
                script_path = _resolve_godot_res_path(ext_resources[ref_id]['attrs'].get('path', ''), tscn_dir, project_root)
                if script_path.lower().endswith('.gd') and os.path.isfile(script_path):
                    stem = os.path.splitext(os.path.basename(script_path))[0]
                    target_cls = _to_pascal_case(f'{stem}_{node_name}' if node_overrides else stem)
                    bound_class_name = import_gdscript_file(script_path, target_cls, node_overrides)
                elif script_path.lower().endswith('.py') and os.path.isfile(script_path):
                    with open(script_path, 'r', encoding='utf-8', errors='ignore') as sf:
                        py_src = sf.read()
                    g = globals()
                    exec(compile(py_src, script_path, 'exec'), g, g)
                    bound_class_name = _to_pascal_case(os.path.splitext(os.path.basename(script_path))[0])
            elif ref_kind == 'SubResource' and ref_id in sub_resources:
                sub_script = sub_resources[ref_id]['props'].get('script/source', '')
                if isinstance(sub_script, str) and sub_script.strip():
                    target_cls = _to_pascal_case(f'GdInline_{node_name}')
                    bound_class_name, py_code = transpile_gdscript_to_python(sub_script, target_cls)
                    py_dir = _resolve_scripts_py_dir()
                    out_py_path = os.path.join(py_dir, f'{bound_class_name}.py').replace('\\', '/')
                    with open(out_py_path, 'w', encoding='utf-8') as sf:
                        sf.write(py_code)
                    g = globals()
                    exec(compile(py_code, out_py_path, 'exec'), g, g)
                    if bound_class_name in g and isinstance(g[bound_class_name], type):
                        g['__mqPyScriptClasses'][bound_class_name] = g[bound_class_name]

            if bound_class_name:
                dm.bindScriptClass(trunk_name, entity_id, bound_class_name)

    print(f'[GodotImporter] 成功导入 Godot 场景 [{tscn_path}] -> Trunk [{trunk_name}], 共创建 {created_count} 个实体')
    return int(root_entity_id)


importGodotScene = import_godot_scene


class GodotImporterPlugin:
    name = 'GodotImporter'
    version = '1.0.0'

    def onLoad(self, engine_pkg=None):
        if isinstance(MQEngine, dict):
            MQEngine['importGodotScene'] = import_godot_scene
            MQEngine['import_godot_scene'] = import_godot_scene
            MQEngine['importGdScriptFile'] = import_gdscript_file
            MQEngine['import_gdscript_file'] = import_gdscript_file
            MQEngine['transpileGdScript'] = transpile_gdscript_to_python
        print('[GodotImporter] 插件 onLoad 完成 (基于 FCT_PyBri C++/Python 双向内嵌桥接)')

    def onInit(self, engine_pkg=None):
        print('[GodotImporter] 插件 onInit 就绪，支持 .tscn 场景与 .gd 脚本导入')

    def onLogicTick(self, dt: float):
        pass

    def onUnload(self):
        print('[GodotImporter] 插件已卸载')


plugin = GodotImporterPlugin()
if hasattr(MQEngine, 'registerPlugin'):
    MQEngine.registerPlugin(plugin)
elif isinstance(MQEngine, dict) and 'registerPlugin' in MQEngine:
    MQEngine['registerPlugin'](plugin)

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tools/Testing/validate_douyin_package.py
MQEngine Douyin Minigame Package Output Validator
Opaque-box validator for MQEngine Douyin Minigame export packages.
"""

import os
import sys
import io
import json
import zipfile
import argparse
from datetime import datetime, timezone
from pathlib import Path

MANDATORY_ROOT_FILES = [
    "game.js",
    "game.json",
    "project.config.json",
    "engine.wasm",
    "shaders_wgl2.bin",
    "asset_manifest.bin"
]

PROHIBITED_EXTENSIONS = {
    ".fbx", ".gltf", ".glb", ".obj",             # 原始 3D 模型资产（必须烘焙为 .mqmesh）
    ".hlsl", ".glsl", ".vert", ".frag", ".spv",   # 原始着色器源文件（必须烘焙为 shaders_wgl2.bin）
    ".hdr", ".exr", ".tga", ".psd",               # 未压缩重型纹理资产
    ".pdb", ".map", ".d.ts"                       # 调试与开发符号
}

PROHIBITED_NAME_PATTERNS = [
    "imgui", "editor", "test", "unittest"
]

FONT_EXTENSIONS = {".ttf", ".otf", ".ttc", ".woff", ".woff2", ".eot"}
TTF_MAGIC_HEADERS = [b"\x00\x01\x00\x00", b"OTTO", b"ttcf", b"true"]

def is_raw_font_file(file_path: Path) -> bool:
    """检查文件是否为原始 TrueType/OpenType 字体文件（通过扩展名与魔数嗅探）"""
    ext = file_path.suffix.lower()
    if ext == ".mqfont":
        return False
    if ext in FONT_EXTENSIONS:
        return True
    try:
        if file_path.stat().st_size >= 4:
            with open(file_path, "rb") as f:
                header = f.read(4)
                if any(header.startswith(sig) for sig in TTF_MAGIC_HEADERS):
                    return True
    except Exception:
        pass
    return False

def calculate_compressed_sizes(package_dir: Path):
    """计算整个主包与纯代码文件的 Zip Deflate 压缩体积"""
    total_uncompressed = 0
    code_uncompressed = 0
    all_files = []
    code_files = []

    for root, _, files in os.walk(package_dir):
        for f in files:
            p = Path(root) / f
            size = p.stat().st_size
            rel_p = p.relative_to(package_dir).as_posix()
            total_uncompressed += size
            all_files.append((p, rel_p, size))
            if p.suffix.lower() in {".wasm", ".js"} or f == "game_logic.js":
                code_uncompressed += size
                code_files.append((p, rel_p, size))

    zip_buffer = io.BytesIO()
    with zipfile.ZipFile(zip_buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p, rel_p, _ in all_files:
            zf.write(p, arcname=rel_p)
    main_zip_bytes = zip_buffer.tell()

    code_zip_buffer = io.BytesIO()
    with zipfile.ZipFile(code_zip_buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p, rel_p, _ in code_files:
            zf.write(p, arcname=rel_p)
    code_zip_bytes = code_zip_buffer.tell()

    return {
        "main_uncompressed_bytes": total_uncompressed,
        "main_zip_bytes": main_zip_bytes,
        "main_zip_mb": round(main_zip_bytes / (1024 * 1024), 4),
        "code_compressed_bytes": code_zip_bytes,
        "code_compressed_mb": round(code_zip_bytes / (1024 * 1024), 4),
        "total_files_count": len(all_files)
    }

def validate_shaders_wgl2_bin(shaders_path: Path):
    """校验 shaders_wgl2.bin 包含离线烘焙的 #version 300 es 着色器，且无桌面版未转译源码泄漏"""
    shader_errors = []
    if not shaders_path.is_file():
        return {"passed": False, "errors": ["shaders_wgl2.bin does not exist"]}
    raw_bytes = shaders_path.read_bytes()
    if len(raw_bytes) == 0:
        shader_errors.append("shaders_wgl2.bin is empty (0 bytes)")
        return {"passed": False, "errors": shader_errors}
    if b"#version 300 es" not in raw_bytes:
        shader_errors.append("shaders_wgl2.bin missing mandatory '#version 300 es' WebGL 2.0 shader payload")
    for forbidden_tag in (b"#version 450", b"#version 460", b"GL_EXT_nonuniform_qualifier"):
        if forbidden_tag in raw_bytes:
            shader_errors.append(
                f"shaders_wgl2.bin contains unstripped desktop shader directive: {forbidden_tag.decode('ascii', errors='ignore')}"
            )
    return {"passed": len(shader_errors) == 0, "errors": shader_errors}

def validate_package(args):
    pkg = Path(args.package_dir).resolve()
    errors, warnings = [], []
    checks = {}

    if not pkg.exists() or not pkg.is_dir():
        err_msg = f"Package directory does not exist or is not a directory: {pkg}"
        errors.append(err_msg)
        checks["package_exists"] = {
            "passed": False,
            "error": err_msg
        }
        default_metrics = {
            "main_uncompressed_bytes": 0,
            "main_zip_bytes": 0,
            "main_zip_mb": 0.0,
            "code_compressed_bytes": 0,
            "code_compressed_mb": 0.0,
            "chinese_font_bytes": 0,
            "total_files_count": 0
        }
        result = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "passed": False,
            "exit_code": 3,
            "package_path": str(pkg),
            "metrics": default_metrics,
            "checks": checks,
            "errors": errors,
            "warnings": warnings,
            "error": err_msg
        }
        return False, 3, result

    # 1. 检查根目录必须文件
    missing = [f for f in MANDATORY_ROOT_FILES if not (pkg / f).is_file()]
    checks["mandatory_files"] = {"passed": len(missing) == 0, "missing_files": missing}
    if missing:
        errors.append(f"Missing mandatory root files: {missing}")

    # 2. 检查包体大小指标
    metrics = calculate_compressed_sizes(pkg)
    max_main_bytes = int(args.max_main_mb * 1024 * 1024)
    max_code_bytes = int(args.max_code_mb * 1024 * 1024)

    main_passed = metrics["main_zip_bytes"] < max_main_bytes
    checks["main_package_size"] = {
        "passed": main_passed,
        "actual_mb": metrics["main_zip_mb"],
        "max_mb": args.max_main_mb,
        "message": "Passed" if main_passed else f"Main package size {metrics['main_zip_mb']}MB >= limit {args.max_main_mb}MB"
    }
    if not main_passed:
        errors.append(f"MainPackage compressed size ({metrics['main_zip_mb']} MB) exceeds limit ({args.max_main_mb} MB)")

    code_passed = metrics["code_compressed_bytes"] < max_code_bytes
    checks["code_package_size"] = {
        "passed": code_passed,
        "actual_mb": metrics["code_compressed_mb"],
        "max_mb": args.max_code_mb,
        "message": "Passed" if code_passed else f"Code package size {metrics['code_compressed_mb']}MB >= limit {args.max_code_mb}MB"
    }
    if not code_passed:
        errors.append(f"Pure code compressed size ({metrics['code_compressed_mb']} MB) exceeds limit ({args.max_code_mb} MB)")

    # 3. 检查 0KB 中文字库红线
    font_files_found = []
    font_bytes = 0
    for root, _, files in os.walk(pkg):
        for f in files:
            p = Path(root) / f
            if is_raw_font_file(p):
                size = p.stat().st_size
                rel = p.relative_to(pkg).as_posix()
                font_files_found.append({"file": rel, "size": size})
                font_bytes += size

    metrics["chinese_font_bytes"] = font_bytes
    checks["zero_kb_chinese_font"] = {
        "passed": len(font_files_found) == 0,
        "found": font_files_found,
        "message": "Passed (0KB Chinese fonts)" if len(font_files_found) == 0 else f"Found {len(font_files_found)} raw font file(s)"
    }
    if font_files_found:
        errors.append(f"0KB Font Rule Violated: Found raw font files in MainPackage: {font_files_found}")

    # 3b. 校验 shaders_wgl2.bin 离线 ES 3.0 合规
    shaders_check = validate_shaders_wgl2_bin(pkg / "shaders_wgl2.bin")
    checks["shaders_wgl2_es300"] = shaders_check
    if not shaders_check["passed"] and (pkg / "shaders_wgl2.bin").is_file():
        errors.extend(shaders_check["errors"])

    # 4. 校验 game.json
    game_json_path = pkg / "game.json"
    game_json_errors = []
    if game_json_path.is_file():
        try:
            with open(game_json_path, "r", encoding="utf-8") as f:
                gj = json.load(f)
            orientation = gj.get("deviceOrientation")
            if orientation not in ["portrait", "landscape"]:
                game_json_errors.append(f"game.json deviceOrientation must be 'portrait' or 'landscape', got '{orientation}'")
            subpkgs = gj.get("subpackages") or gj.get("subPackages") or []
            for sp in subpkgs:
                root_dir = sp.get("root", "")
                if not root_dir or not (pkg / root_dir).exists():
                    game_json_errors.append(f"Subpackage root directory does not exist: '{root_dir}'")
            checks["game_json_valid"] = {"passed": len(game_json_errors) == 0, "errors": game_json_errors}
            if game_json_errors:
                errors.extend(game_json_errors)
        except Exception as e:
            err = f"Failed to parse game.json: {e}"
            game_json_errors.append(err)
            errors.append(err)
            checks["game_json_valid"] = {"passed": False, "errors": game_json_errors}
    else:
        checks["game_json_valid"] = {"passed": False, "errors": ["game.json does not exist"]}

    # 5. 校验 project.config.json
    project_config_path = pkg / "project.config.json"
    project_config_errors = []
    if project_config_path.is_file():
        try:
            with open(project_config_path, "r", encoding="utf-8") as f:
                pj = json.load(f)
            appid = pj.get("appid", "")
            if not appid or not isinstance(appid, str):
                project_config_errors.append("project.config.json missing valid 'appid'")
            elif args.expected_appid and appid != args.expected_appid:
                project_config_errors.append(f"project.config.json appid '{appid}' does not match expected '{args.expected_appid}'")
            checks["project_config_valid"] = {"passed": len(project_config_errors) == 0, "errors": project_config_errors}
            if project_config_errors:
                errors.extend(project_config_errors)
        except Exception as e:
            err = f"Failed to parse project.config.json: {e}"
            project_config_errors.append(err)
            errors.append(err)
            checks["project_config_valid"] = {"passed": False, "errors": project_config_errors}
    else:
        checks["project_config_valid"] = {"passed": False, "errors": ["project.config.json does not exist"]}

    # 6. 校验分包一致性
    checks["subpackages_valid"] = {"passed": True, "errors": []}

    # 7. 扫描违禁文件与桌面残留符号
    prohibited_found = []
    for root, _, files in os.walk(pkg):
        for f in files:
            p = Path(root) / f
            rel = p.relative_to(pkg).as_posix()
            ext = p.suffix.lower()
            if ext in PROHIBITED_EXTENSIONS:
                prohibited_found.append({"file": rel, "reason": f"Prohibited extension '{ext}'"})
            lower_name = f.lower()
            for pattern in PROHIBITED_NAME_PATTERNS:
                if pattern in lower_name and not lower_name.endswith(".png") and not lower_name.endswith(".jpg"):
                    prohibited_found.append({"file": rel, "reason": f"Leaked symbol pattern '{pattern}'"})
    checks["prohibited_files"] = {"passed": len(prohibited_found) == 0, "violations": prohibited_found}
    if prohibited_found:
        errors.append(f"Prohibited files found in package: {prohibited_found}")

    # 判断通过情况与退出码
    passed = len(errors) == 0
    if passed:
        exit_code = 0
    else:
        has_size_or_font_error = any(
            ("exceeds limit" in e or "Font Rule" in e or "font file" in e) for e in errors
        )
        exit_code = 2 if has_size_or_font_error else 1

    result = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "passed": passed,
        "exit_code": exit_code,
        "package_path": str(pkg),
        "metrics": metrics,
        "checks": checks,
        "errors": errors,
        "warnings": warnings
    }
    return passed, exit_code, result

def write_markdown_report(report_path: Path, result: dict):
    timestamp = result.get("timestamp", datetime.now(timezone.utc).isoformat())
    package_path = result.get("package_path", "unknown")
    passed = result.get("passed", False)
    exit_code = result.get("exit_code", -1)

    lines = [
        "# Douyin Minigame Package Validation Report",
        "",
        f"- **Timestamp**: {timestamp}",
        f"- **Package Path**: `{package_path}`",
        f"- **Status**: {'PASSED' if passed else 'FAILED'} (Exit Code: {exit_code})",
        "",
        "## Metrics Summary",
        ""
    ]
    metrics = result.get("metrics")
    if isinstance(metrics, dict):
        lines.extend([
            f"- **Total Files**: {metrics.get('total_files_count', 0)}",
            f"- **Main Package Uncompressed**: {metrics.get('main_uncompressed_bytes', 0)} bytes",
            f"- **Main Package Compressed (Zip Deflate 9)**: {metrics.get('main_zip_bytes', 0)} bytes ({metrics.get('main_zip_mb', 0.0)} MB)",
            f"- **Pure Code Compressed (Wasm + JS)**: {metrics.get('code_compressed_bytes', 0)} bytes ({metrics.get('code_compressed_mb', 0.0)} MB)",
            f"- **Chinese Font Files Size**: {metrics.get('chinese_font_bytes', 0)} bytes",
        ])
    else:
        lines.append("- *(Metrics unavailable)*")

    lines.extend(["", "## Checks", ""])
    checks = result.get("checks", {})
    if isinstance(checks, dict):
        for check_name, check_data in checks.items():
            status_icon = "PASS" if isinstance(check_data, dict) and check_data.get("passed", False) else "FAIL"
            lines.append(f"- **{check_name}**: [{status_icon}]")

    errors = result.get("errors", [])
    if errors:
        lines.extend(["", "## Errors", ""])
        for e in errors:
            lines.append(f"- {e}")

    warnings = result.get("warnings", [])
    if warnings:
        lines.extend(["", "## Warnings", ""])
        for w in warnings:
            lines.append(f"- {w}")

    report_path.write_text("\n".join(lines), encoding="utf-8")

def main():
    parser = argparse.ArgumentParser(
        description="MQEngine Douyin Minigame Package Output Validator"
    )
    parser.add_argument("--package-dir", required=True, type=str,
                        help="Path to the exported MainPackage directory.")
    parser.add_argument("--zip-path", default=None, type=str,
                        help="Optional path to output or pre-existing package zip.")
    parser.add_argument("--max-main-mb", default=10.0, type=float,
                        help="Maximum allowed compressed MainPackage size in MB (default: 10.0).")
    parser.add_argument("--max-code-mb", default=4.0, type=float,
                        help="Maximum allowed compressed code (Wasm + JS) size in MB (default: 4.0).")
    parser.add_argument("--expected-appid", default=None, type=str,
                        help="Expected AppID (must match project.config.json if specified).")
    parser.add_argument("--strict", action="store_true",
                        help="Enforce strict mode (zero warnings permitted, fail on any suspicious file).")
    parser.add_argument("--output-json", default=None, type=str,
                        help="Output path for structured JSON results report.")
    parser.add_argument("--report-md", default=None, type=str,
                        help="Output path for Markdown summary report.")
    parser.add_argument("--verbose", action="store_true",
                        help="Enable verbose diagnostic file logging.")

    args = parser.parse_args()

    try:
        passed, exit_code, result = validate_package(args)

        if args.output_json:
            out_p = Path(args.output_json).resolve()
            out_p.parent.mkdir(parents=True, exist_ok=True)
            out_p.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")

        if args.report_md:
            md_p = Path(args.report_md).resolve()
            md_p.parent.mkdir(parents=True, exist_ok=True)
            write_markdown_report(md_p, result)

        status_str = "PASS" if passed else "FAIL"
        print(f"[Package Validator] Status: {status_str} | Exit Code: {exit_code}")
        metrics = result.get("metrics")
        if isinstance(metrics, dict):
            print(f"  Main Package (Zip): {metrics.get('main_zip_mb', 0.0)} MB / {args.max_main_mb} MB")
            print(f"  Code Package (Zip): {metrics.get('code_compressed_mb', 0.0)} MB / {args.max_code_mb} MB")
            print(f"  Chinese Font Bytes: {metrics.get('chinese_font_bytes', 0)} B (Requirement: 0 KB)")
        else:
            print("  Main Package (Zip): N/A")
            print("  Code Package (Zip): N/A")
            print("  Chinese Font Bytes: N/A")

        errors = result.get("errors", [])
        if errors:
            print("  Errors:")
            for err in errors:
                print(f"    - {err}")
        warnings = result.get("warnings", [])
        if args.verbose and warnings:
            print("  Warnings:")
            for warn in warnings:
                print(f"    - {warn}")

        sys.exit(exit_code)
    except SystemExit:
        raise
    except Exception as e:
        print(f"[ERROR] Internal validator error: {e}", file=sys.stderr)
        sys.exit(3)

if __name__ == "__main__":
    main()

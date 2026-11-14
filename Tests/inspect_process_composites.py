import ctypes
from ctypes import wintypes
import os
import sys

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

DEBUG_ONLY_THIS_PROCESS = 0x00000002
DBG_CONTINUE = 0x00010002

class EXCEPTION_RECORD(ctypes.Structure):
    pass
EXCEPTION_RECORD._fields_ = [
    ('ExceptionCode', wintypes.DWORD),
    ('ExceptionFlags', wintypes.DWORD),
    ('ExceptionRecord', ctypes.POINTER(EXCEPTION_RECORD)),
    ('ExceptionAddress', ctypes.c_void_p),
    ('NumberParameters', wintypes.DWORD),
    ('ExceptionInformation', ctypes.c_void_p * 15),
]

class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('ExceptionRecord', EXCEPTION_RECORD),
        ('dwFirstChance', wintypes.DWORD),
    ]

class CREATE_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hThread', wintypes.HANDLE),
        ('lpThreadLocalBase', ctypes.c_void_p),
        ('lpStartAddress', ctypes.c_void_p),
    ]

class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hFile', wintypes.HANDLE),
        ('hProcess', wintypes.HANDLE),
        ('hThread', wintypes.HANDLE),
        ('lpBaseOfImage', ctypes.c_void_p),
        ('dwDebugInfoFileOffset', wintypes.DWORD),
        ('nDebugInfoSize', wintypes.DWORD),
        ('lpThreadLocalBase', ctypes.c_void_p),
        ('lpStartAddress', ctypes.c_void_p),
        ('lpImageName', ctypes.c_void_p),
        ('fUnicode', wintypes.WORD),
    ]

class EXIT_THREAD_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('dwExitCode', wintypes.DWORD)]

class EXIT_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('dwExitCode', wintypes.DWORD)]

class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hFile', wintypes.HANDLE),
        ('lpBaseOfDll', ctypes.c_void_p),
        ('dwDebugInfoFileOffset', wintypes.DWORD),
        ('nDebugInfoSize', wintypes.DWORD),
        ('lpImageName', ctypes.c_void_p),
        ('fUnicode', wintypes.WORD),
    ]

class UNLOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('lpBaseOfDll', ctypes.c_void_p)]

class OUTPUT_DEBUG_STRING_INFO(ctypes.Structure):
    _fields_ = [
        ('lpDebugStringData', ctypes.c_char_p),
        ('fUnicode', wintypes.WORD),
        ('nDebugStringLength', wintypes.WORD),
    ]

class RIP_INFO(ctypes.Structure):
    _fields_ = [('dwError', wintypes.DWORD), ('dwType', wintypes.DWORD)]

class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [
        ('Exception', EXCEPTION_DEBUG_INFO),
        ('CreateThread', CREATE_THREAD_DEBUG_INFO),
        ('CreateProcessInfo', CREATE_PROCESS_DEBUG_INFO),
        ('ExitThread', EXIT_THREAD_DEBUG_INFO),
        ('ExitProcess', EXIT_PROCESS_DEBUG_INFO),
        ('LoadDll', LOAD_DLL_DEBUG_INFO),
        ('UnloadDll', UNLOAD_DLL_DEBUG_INFO),
        ('DebugString', OUTPUT_DEBUG_STRING_INFO),
        ('RipInfo', RIP_INFO),
    ]

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ('dwDebugEventCode', wintypes.DWORD),
        ('dwProcessId', wintypes.DWORD),
        ('dwThreadId', wintypes.DWORD),
        ('u', DEBUG_EVENT_UNION),
    ]

class STARTUPINFO(ctypes.Structure):
    _fields_ = [
        ('cb', wintypes.DWORD),
        ('lpReserved', wintypes.LPWSTR),
        ('lpDesktop', wintypes.LPWSTR),
        ('lpTitle', wintypes.LPWSTR),
        ('dwX', wintypes.DWORD),
        ('dwY', wintypes.DWORD),
        ('dwXSize', wintypes.DWORD),
        ('dwYSize', wintypes.DWORD),
        ('dwXCountChars', wintypes.DWORD),
        ('dwYCountChars', wintypes.DWORD),
        ('dwFillAttribute', wintypes.DWORD),
        ('dwFlags', wintypes.DWORD),
        ('wShowWindow', wintypes.WORD),
        ('cbReserved2', wintypes.WORD),
        ('lpReserved2', ctypes.POINTER(wintypes.BYTE)),
        ('hStdInput', wintypes.HANDLE),
        ('hStdOutput', wintypes.HANDLE),
        ('hStdError', wintypes.HANDLE),
    ]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ('hProcess', wintypes.HANDLE),
        ('hThread', wintypes.HANDLE),
        ('dwProcessId', wintypes.DWORD),
        ('dwThreadId', wintypes.DWORD),
    ]

def get_file_path(hFile):
    if not hFile:
        return ''
    buf = ctypes.create_unicode_buffer(1024)
    res = kernel32.GetFinalPathNameByHandleW(hFile, buf, 1024, 0)
    if res > 0:
        p = buf.value
        if p.startswith(r'\\?\\'):
            p = p[4:]
        return p
    return ''

def run_and_trace_dlls(cmd, cwd):
    si = STARTUPINFO()
    si.cb = ctypes.sizeof(STARTUPINFO)
    pi = PROCESS_INFORMATION()

    ok = kernel32.CreateProcessW(
        None, cmd, None, None, False,
        DEBUG_ONLY_THIS_PROCESS, None, cwd,
        ctypes.byref(si), ctypes.byref(pi)
    )
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())

    loaded_dlls = []
    unloaded_dlls = []
    exit_code = None

    evt = DEBUG_EVENT()
    while True:
        if not kernel32.WaitForDebugEvent(ctypes.byref(evt), 60000):
            break

        code = evt.dwDebugEventCode
        if code == 6:  # LOAD_DLL_DEBUG_EVENT
            dll_path = get_file_path(evt.u.LoadDll.hFile)
            if dll_path:
                loaded_dlls.append(dll_path)
        elif code == 7:  # UNLOAD_DLL_DEBUG_EVENT
            unloaded_dlls.append(evt.u.UnloadDll.lpBaseOfDll)
        elif code == 5:  # EXIT_PROCESS_DEBUG_EVENT
            exit_code = evt.u.ExitProcess.dwExitCode

        kernel32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, DBG_CONTINUE)

        if code == 5:
            break

    kernel32.CloseHandle(pi.hProcess)
    kernel32.CloseHandle(pi.hThread)
    return exit_code, loaded_dlls, len(unloaded_dlls)

def main():
    base_dir = r'G:\MQEngine\MQEngine\Output\build-windows-debug'
    
    print('=' * 80)
    print('EMPIRICAL RUNTIME VERIFICATION: CompositeManager DLL Discovery & Tag Filtering')
    print('=' * 80)

    # 1. Editor.exe verification
    print('\n[1] Testing Editor.exe with tags: [Runtime, Editor, ImGui]...')
    editor_exe = os.path.join(base_dir, 'Applications', 'Editor', 'Editor.exe')
    editor_cwd = os.path.join(base_dir, 'Applications', 'Editor')
    editor_cmd = f'"{editor_exe}" --headless --test-frames 30'

    code, dlls, unloads = run_and_trace_dlls(editor_cmd, editor_cwd)
    composite_dlls = [d for d in dlls if 'Composite' in os.path.basename(d)]
    composite_names = set(os.path.basename(d) for d in composite_dlls)

    print(f'  Editor Exit Code: {code}')
    print(f'  Total Loaded DLLs: {len(dlls)}, Total DLL Unloads on Exit: {unloads}')
    print(f'  Loaded Composite DLLs count: {len(composite_names)}')
    for d in sorted(composite_names):
        print(f'    [LOADED] {d}')

    expected_editor_composites = {
        'ImGuiComposite.dll',
        'EditorUiComposite.dll',
        'SceneViewerComposite.dll',
        'AssetBrowserComposite.dll',
        'ShaderEditorComposite.dll',
        'HtmlUiEditorComposite.dll'
    }

    editor_ok = True
    for expected in expected_editor_composites:
        if expected not in composite_names:
            print(f'  [FAIL] Missing expected Editor composite DLL: {expected}')
            editor_ok = False
        else:
            print(f'  [PASS] Expected Editor composite verified: {expected}')

    if 'RmlUiComposite.dll' in composite_names:
        print('  [FAIL] RmlUiComposite.dll was loaded in Editor (should have been filtered out!)')
        editor_ok = False
    else:
        print('  [PASS] RmlUiComposite.dll was properly excluded from Editor per tags!')

    if code != 0:
        print(f'  [FAIL] Editor.exe exited with non-zero code {code}')
        editor_ok = False

    # 2. Game.exe verification
    print('\n[2] Testing Game.exe with tags: [Runtime, Game]...')
    game_exe = os.path.join(base_dir, 'Applications', 'Game', 'Game.exe')
    game_cwd = os.path.join(base_dir, 'Applications', 'Game')
    game_cmd = f'"{game_exe}" --headless --test-frames 30'

    code_game, dlls_game, unloads_game = run_and_trace_dlls(game_cmd, game_cwd)
    composite_dlls_game = [d for d in dlls_game if 'Composite' in os.path.basename(d)]
    composite_names_game = set(os.path.basename(d) for d in composite_dlls_game)

    print(f'  Game Exit Code: {code_game}')
    print(f'  Total Loaded DLLs: {len(dlls_game)}, Total DLL Unloads on Exit: {unloads_game}')
    print(f'  Loaded Composite DLLs count: {len(composite_names_game)}')
    for d in sorted(composite_names_game):
        print(f'    [LOADED] {d}')

    game_ok = True
    if 'RmlUiComposite.dll' not in composite_names_game:
        print('  [FAIL] RmlUiComposite.dll was NOT loaded in Game!')
        game_ok = False
    else:
        print('  [PASS] RmlUiComposite.dll was loaded in Game per activeTags [Runtime, Game]')

    editor_only = [
        'EditorUiComposite.dll',
        'SceneViewerComposite.dll',
        'AssetBrowserComposite.dll',
        'ShaderEditorComposite.dll',
        'HtmlUiEditorComposite.dll'
    ]
    for e in editor_only:
        if e in composite_names_game:
            print(f'  [FAIL] Editor-only composite {e} was mistakenly loaded in Game!')
            game_ok = False
        else:
            print(f'  [PASS] Editor-only composite {e} was properly excluded in Game!')

    if code_game != 0:
        print(f'  [FAIL] Game.exe exited with non-zero code {code_game}')
        game_ok = False

    print('\n' + '=' * 80)
    if editor_ok and game_ok:
        print('>>> EMPIRICAL PROCESS & COMPOSITE TAG FILTERING VERIFICATION PASSED! <<<')
        print('=' * 80)
        sys.exit(0)
    else:
        print('>>> VERIFICATION FAILED! <<<')
        print('=' * 80)
        sys.exit(1)

if __name__ == '__main__':
    main()

"""
================================================================================
 CS2 Vibrance & Sharpness Utility (Python Standalone Version)
 Otimizado para Windows 10/11 x64 e NVIDIA GeForce (ex: RTX 3060 Ti)
================================================================================
Características:
 1. 100% livre de injeção de DLL / código (100% VAC Safe).
 2. Acesso direto a nvapi64.dll via ctypes (sem dependências C++ extras).
 3. Detecção passiva de processo em primeiro plano (cs2.exe).
 4. Detecção de tela cheia vs modo janela.
 5. Persistência de configurações em %APPDATA%/CS2Vibrance/config.json.
 6. Suporte a múltiplos monitores NVIDIA ativos.
 7. Suporte a Nitidez (Sharpness) com fallback gracioso.
================================================================================
"""

import os
import sys
import time
import json
import ctypes
from ctypes import wintypes

# --- CONSTANTES NVAPI ---
NVAPI_INITIALIZE = 0x0150E828
NVAPI_UNLOAD = 0xD22BDD7E
NVAPI_ENUM_DISPLAY_HANDLE = 0x9ABDD40D
NVAPI_GET_DVC_INFO = 0x4085DE45
NVAPI_SET_DVC_LEVEL = 0x172409B4
NVAPI_OVERLAY_SHARPNESS = 0x7A2BC2B2

class NV_DISPLAY_DVC_INFO(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("version", wintypes.DWORD),
        ("currentLevel", wintypes.INT),
        ("minLevel", wintypes.INT),
        ("maxLevel", wintypes.INT),
    ]

# Assinaturas de tipos C para NVAPI
PROTO_INT = ctypes.WINFUNCTYPE(ctypes.c_int)
PROTO_ENUM_DISP = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))
PROTO_GET_DVC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(NV_DISPLAY_DVC_INFO))
PROTO_SET_DVC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
PROTO_SHARPNESS = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int)

# --- CARREGAMENTO DA NVAPI ---
nvapi = None
for dll_name in ("nvapi64.dll", "nvapi.dll"):
    try:
        nvapi = ctypes.cdll.LoadLibrary(dll_name)
        break
    except Exception:
        pass

if not nvapi:
    print("[ERRO] Driver NVIDIA não encontrado ou nvapi64.dll inacessível.")
    print("Certifique-se de que o driver NVIDIA GeForce Game Ready está instalado.")
    sys.exit(1)

nvapi_QueryInterface = nvapi.nvapi_QueryInterface
nvapi_QueryInterface.restype = ctypes.c_void_p
nvapi_QueryInterface.argtypes = [ctypes.c_uint]

def get_nv_func(func_id, prototype):
    ptr = nvapi_QueryInterface(func_id)
    return prototype(ptr) if ptr else None

nv_init = get_nv_func(NVAPI_INITIALIZE, PROTO_INT)
nv_unload = get_nv_func(NVAPI_UNLOAD, PROTO_INT)
nv_enum_display = get_nv_func(NVAPI_ENUM_DISPLAY_HANDLE, PROTO_ENUM_DISP)
nv_get_dvc = get_nv_func(NVAPI_GET_DVC_INFO, PROTO_GET_DVC)
nv_set_dvc = get_nv_func(NVAPI_SET_DVC_LEVEL, PROTO_SET_DVC)
nv_set_sharpness = get_nv_func(NVAPI_OVERLAY_SHARPNESS, PROTO_SHARPNESS)

if not nv_init or nv_init() != 0:
    print("[ERRO] Falha ao inicializar NVAPI via NvAPI_Initialize.")
    sys.exit(1)

# Encontrar monitores ativos NVIDIA
display_handles = []
if nv_enum_display:
    for i in range(16):
        h = ctypes.c_void_p()
        if nv_enum_display(i, ctypes.byref(h)) == 0 and h.value:
            display_handles.append(h)

if not display_handles:
    print("[AVISO] Nenhum monitor ativo detectado pela NVAPI.")
else:
    print(f"[OK] {len(display_handles)} monitor(es) NVIDIA identificado(s).")

def get_dvc_bounds(h_disp):
    info = NV_DISPLAY_DVC_INFO()
    info.version = ctypes.sizeof(NV_DISPLAY_DVC_INFO) | 0x10000
    if nv_get_dvc and nv_get_dvc(h_disp, 0, ctypes.byref(info)) == 0:
        return info.minLevel, info.maxLevel, info.currentLevel
    return 0, 63, 0

def apply_vibrance(percent):
    percent = max(0, min(100, percent))
    for h_disp in display_handles:
        min_lvl, max_lvl, _ = get_dvc_bounds(h_disp)
        level = int(min_lvl + (max_lvl - min_lvl) * (percent / 100.0) + 0.5)
        if nv_set_dvc:
            nv_set_dvc(h_disp, 0, level)

def apply_sharpness(percent):
    if not nv_set_sharpness:
        return False
    percent = max(0, min(100, percent))
    for h_disp in display_handles:
        nv_set_sharpness(h_disp, percent)
    return True

# --- WINDOWS API ---
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

class RECT(ctypes.Structure):
    _fields_ = [
        ("left", wintypes.LONG),
        ("top", wintypes.LONG),
        ("right", wintypes.LONG),
        ("bottom", wintypes.LONG),
    ]

class MONITORINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("rcMonitor", RECT),
        ("rcWork", RECT),
        ("dwFlags", wintypes.DWORD),
    ]

def get_foreground_process_info():
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return "", None
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    h_proc = kernel32.OpenProcess(0x1000, False, pid.value)
    if not h_proc:
        return "", hwnd
    buf = (ctypes.c_wchar * 260)()
    size = wintypes.DWORD(260)
    try:
        if kernel32.QueryFullProcessImageNameW(h_proc, 0, buf, ctypes.byref(size)):
            return os.path.basename(buf.value).lower(), hwnd
    finally:
        kernel32.CloseHandle(h_proc)
    return "", hwnd

def is_window_fullscreen(hwnd):
    if not hwnd:
        return False
    rc = RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rc)):
        return True
    win_w = rc.right - rc.left
    win_h = rc.bottom - rc.top

    h_mon = user32.MonitorFromWindow(hwnd, 2) # MONITOR_DEFAULTTONEAREST
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    if user32.GetMonitorInfoW(h_mon, ctypes.byref(mi)):
        mon_w = mi.rcMonitor.right - mi.rcMonitor.left
        mon_h = mi.rcMonitor.bottom - mi.rcMonitor.top
        return win_w >= mon_w and win_h >= mon_h
    return True

# --- CONFIGURAÇÕES ---
def get_config_dir():
    appdata = os.environ.get("APPDATA", os.path.expanduser("~"))
    config_dir = os.path.join(appdata, "CS2Vibrance")
    os.makedirs(config_dir, exist_ok=True)
    return config_dir

def load_config():
    config_path = os.path.join(get_config_dir(), "config.json")
    default_config = {
        "windows_vibrance": 50,
        "cs2_vibrance": 100,
        "sharpness_percent": 0,
        "target_process": "cs2.exe",
        "apply_in_windowed": True,
        "check_interval_seconds": 0.5
    }
    if os.path.exists(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                default_config.update(data)
        except Exception:
            pass
    else:
        try:
            with open(config_path, "w", encoding="utf-8") as f:
                json.dump(default_config, f, indent=4)
        except Exception:
            pass
    return default_config

def main():
    config = load_config()
    win_vib = config["windows_vibrance"]
    cs2_vib = config["cs2_vibrance"]
    sharpness = config["sharpness_percent"]
    target_proc = config["target_process"].lower()
    apply_windowed = config["apply_in_windowed"]
    interval = config["check_interval_seconds"]

    print("==================================================")
    print("      CS2 VIBRANCE & SHARPNESS AUTO (PYTHON)      ")
    print(f"  Monitorando        : {target_proc}")
    print(f"  Vibrance Windows   : {win_vib}%")
    print(f"  Vibrance CS2       : {cs2_vib}%")
    print(f"  Nitidez (Sharpness): {sharpness}%")
    print(f"  Modo Janela        : {'Sim' if apply_windowed else 'Apenas Tela Cheia'}")
    print("==================================================")
    print("Pressione Ctrl+C para encerrar com restauração padrão.")

    is_active = False

    try:
        while True:
            proc, hwnd = get_foreground_process_info()
            matches = (proc == target_proc)

            if matches and not apply_windowed:
                matches = is_window_fullscreen(hwnd)

            if matches:
                if not is_active:
                    apply_vibrance(cs2_vib)
                    if sharpness > 0:
                        apply_sharpness(sharpness)
                    print(f"[+] {target_proc} em foco -> Vibrance: {cs2_vib}% | Nitidez: {sharpness}%")
                    is_active = True
            else:
                if is_active:
                    apply_vibrance(win_vib)
                    if sharpness > 0:
                        apply_sharpness(0)
                    print(f"[-] {target_proc} fora de foco -> Vibrance restaurada: {win_vib}%")
                    is_active = False

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n[Encerrando] Restaurando perfil do Windows...")
        apply_vibrance(win_vib)
        apply_sharpness(0)
        if nv_unload:
            nv_unload()
        print("[OK] Perfil restaurado e NVAPI descarregada com sucesso.")

if __name__ == "__main__":
    main()

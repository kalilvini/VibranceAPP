"""
================================================================================
 VibranceApp - Interface Gráfica Amigável & Didática (Python / Tkinter)
 Controle Inteligente de Digital Vibrance e Nitidez por Jogo (NVIDIA GeForce)
================================================================================
"""

import os
import sys
import json
import time
import threading
import ctypes
from ctypes import wintypes
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

# --- NVAPI (NVIDIA) ---
NVAPI_INITIALIZE = 0x0150E828
NVAPI_UNLOAD = 0xD22BDD7E
NVAPI_ENUM_DISPLAY_HANDLE = 0x9ABDD40D
NVAPI_GET_DVC_INFO = 0x4085DE45
NVAPI_SET_DVC_LEVEL = 0x172409B4
NVAPI_OVERLAY_SHARPNESS = 0x7A2BC2B2
NVAPI_ENUM_PHYSICAL_GPUS = 0xE5AC921F
NVAPI_GPU_GET_FULL_NAME = 0xCEEE8E9F

class NV_DISPLAY_DVC_INFO(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("version", wintypes.DWORD),
        ("currentLevel", wintypes.INT),
        ("minLevel", wintypes.INT),
        ("maxLevel", wintypes.INT),
    ]

PROTO_INT = ctypes.WINFUNCTYPE(ctypes.c_int)
PROTO_ENUM_DISP = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))
PROTO_GET_DVC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(NV_DISPLAY_DVC_INFO))
PROTO_SET_DVC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
PROTO_SHARPNESS = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int)

nvapi = None
for dll in ("nvapi64.dll", "nvapi.dll"):
    try:
        nvapi = ctypes.cdll.LoadLibrary(dll)
        break
    except Exception:
        pass

nvapi_ready = False
display_handles = []
gpu_name_str = "NVIDIA GeForce (Detectando...)"

if nvapi:
    try:
        nvapi_QueryInterface = nvapi.nvapi_QueryInterface
        nvapi_QueryInterface.restype = ctypes.c_void_p
        nvapi_QueryInterface.argtypes = [ctypes.c_uint]

        def get_nv(id_, proto):
            ptr = nvapi_QueryInterface(id_)
            return proto(ptr) if ptr else None

        nv_init = get_nv(NVAPI_INITIALIZE, PROTO_INT)
        nv_unload = get_nv(NVAPI_UNLOAD, PROTO_INT)
        nv_enum_disp = get_nv(NVAPI_ENUM_DISPLAY_HANDLE, PROTO_ENUM_DISP)
        nv_get_dvc = get_nv(NVAPI_GET_DVC_INFO, PROTO_GET_DVC)
        nv_set_dvc = get_nv(NVAPI_SET_DVC_LEVEL, PROTO_SET_DVC)
        nv_set_sharpness = get_nv(NVAPI_OVERLAY_SHARPNESS, PROTO_SHARPNESS)

        if nv_init and nv_init() == 0:
            for i in range(16):
                h = ctypes.c_void_p()
                if nv_enum_disp and nv_enum_disp(i, ctypes.byref(h)) == 0 and h.value:
                    display_handles.append(h)
            nvapi_ready = len(display_handles) > 0
    except Exception:
        nvapi_ready = False

def apply_vibrance(percent):
    if not nvapi_ready:
        return
    percent = max(0, min(100, percent))
    for h in display_handles:
        info = NV_DISPLAY_DVC_INFO()
        info.version = ctypes.sizeof(NV_DISPLAY_DVC_INFO) | 0x10000
        min_lvl, max_lvl = 0, 63
        if nv_get_dvc and nv_get_dvc(h, 0, ctypes.byref(info)) == 0:
            min_lvl, max_lvl = info.minLevel, info.maxLevel
        level = int(min_lvl + (max_lvl - min_lvl) * (percent / 100.0) + 0.5)
        if nv_set_dvc:
            nv_set_dvc(h, 0, level)

def apply_sharpness(percent):
    if not nvapi_ready or not nv_set_sharpness:
        return
    percent = max(0, min(100, percent))
    for h in display_handles:
        nv_set_sharpness(h, percent)

# --- DETECÇÃO DE PROCESSOS ---
user32 = ctypes.windll.user32 if hasattr(ctypes, "windll") else None
kernel32 = ctypes.windll.kernel32 if hasattr(ctypes, "windll") else None

def get_foreground_process():
    if not user32 or not kernel32:
        return ""
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return ""
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    h_proc = kernel32.OpenProcess(0x1000, False, pid.value)
    if not h_proc:
        return ""
    buf = (ctypes.c_wchar * 260)()
    size = wintypes.DWORD(260)
    try:
        if kernel32.QueryFullProcessImageNameW(h_proc, 0, buf, ctypes.byref(size)):
            return os.path.basename(buf.value).lower()
    finally:
        kernel32.CloseHandle(h_proc)
    return ""

# --- CONFIGURAÇÃO ---
CONFIG_PATH = os.path.join(os.environ.get("APPDATA", "."), "VibranceAPP", "profiles.json")

DEFAULT_PROFILES = [
    {"name": "Counter-Strike 2", "exe": "cs2.exe", "vibrance": 100, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Valorant", "exe": "valorant-win64-shipping.exe", "vibrance": 85, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Apex Legends", "exe": "r5apex.exe", "vibrance": 80, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Call of Duty / Warzone", "exe": "cod.exe", "vibrance": 85, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Fortnite", "exe": "fortniteclient-win64-shipping.exe", "vibrance": 75, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Overwatch 2", "exe": "overwatch.exe", "vibrance": 80, "sharpness": 0, "windowed": True, "enabled": True},
    {"name": "Rainbow Six Siege", "exe": "rainbowsix.exe", "vibrance": 85, "sharpness": 0, "windowed": True, "enabled": True}
]

def load_data():
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {"windows_vibrance": 50, "profiles": DEFAULT_PROFILES}

def save_data(data):
    try:
        os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4, ensure_ascii=False)
    except Exception as e:
        print("Erro ao salvar:", e)

class VibranceAppGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("VibranceApp - Controle de Saturação & Nitidez")
        self.root.geometry("820x660")
        self.root.configure(bg="#121214")

        self.data = load_data()
        self.profiles = self.data.get("profiles", DEFAULT_PROFILES)
        self.win_vibrance = self.data.get("windows_vibrance", 50)
        self.active_profile = None

        self.setup_ui()
        self.start_monitor()

    def setup_ui(self):
        # Header
        header = tk.Frame(self.root, bg="#18181c", height=70)
        header.pack(fill=tk.X, side=tk.TOP)

        title = tk.Label(header, text="VIBRANCE APP", font=("Segoe UI", 16, "bold"), fg="#ffffff", bg="#18181c")
        title.pack(anchor="w", padx=25, pady=(10, 0))

        sub = tk.Label(header, text="Gerenciador Inteligente de Saturação & Nitidez para Jogos Competitivos", font=("Segoe UI", 9), fg="#9ca3af", bg="#18181c")
        sub.pack(anchor="w", padx=25, pady=(0, 10))

        # Tabs
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TNotebook", background="#121214", borderwidth=0)
        style.configure("TNotebook.Tab", background="#1e1e24", foreground="#d1d5db", padding=[15, 6], font=("Segoe UI", 10, "bold"))
        style.map("TNotebook.Tab", background=[("selected", "#3b82f6")], foreground=[("selected", "#ffffff")])

        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)

        # Tab 1: Perfis de Jogos
        self.tab_games = tk.Frame(self.notebook, bg="#121214")
        self.notebook.add(self.tab_games, text="🎮 Jogos & Perfis")
        self.setup_tab_games()

        # Tab 2: Teste ao Vivo
        self.tab_test = tk.Frame(self.notebook, bg="#121214")
        self.notebook.add(self.tab_test, text="🎨 Teste Rápido / Ao Vivo")
        self.setup_tab_test()

        # Tab 3: Guia Didático
        self.tab_guide = tk.Frame(self.notebook, bg="#121214")
        self.notebook.add(self.tab_guide, text="ℹ️ Guia Didático & Segurança")
        self.setup_tab_guide()

        # Status Bar Rodapé
        self.footer = tk.Frame(self.root, bg="#18181c", height=35)
        self.footer.pack(fill=tk.X, side=tk.BOTTOM)

        self.lbl_status = tk.Label(self.footer, text="🟢 STATUS: Em Espera (Windows: 50%)", font=("Segoe UI", 9, "bold"), fg="#22c55e", bg="#18181c")
        self.lbl_status.pack(side=tk.LEFT, padx=25, pady=8)

        status_gpu = f"NVIDIA GeForce ({len(display_handles)} Monitor)" if nvapi_ready else "NVAPI em Modo Demonstração"
        self.lbl_gpu = tk.Label(self.footer, text=status_gpu, font=("Segoe UI", 9), fg="#9ca3af", bg="#18181c")
        self.lbl_gpu.pack(side=tk.RIGHT, padx=25, pady=8)

    def setup_tab_games(self):
        # Lista de Jogos (Esquerda)
        left = tk.Frame(self.tab_games, bg="#121214", width=280)
        left.pack(side=tk.LEFT, fill=tk.BOTH, padx=(10, 10), pady=15)

        tk.Label(left, text="Jogos Cadastrados:", font=("Segoe UI", 10, "bold"), fg="#ffffff", bg="#121214").pack(anchor="w")

        self.listbox = tk.Listbox(left, bg="#1e1e24", fg="#ffffff", font=("Segoe UI", 10), selectbackground="#3b82f6", borderwidth=0, highlightthickness=1, highlightbackground="#2d2d35")
        self.listbox.pack(fill=tk.BOTH, expand=True, pady=8)
        self.listbox.bind("<<ListboxSelect>>", self.on_game_selected)

        btn_row = tk.Frame(left, bg="#121214")
        btn_row.pack(fill=tk.X)
        tk.Button(btn_row, text="+ Novo Jogo", bg="#22c55e", fg="#ffffff", font=("Segoe UI", 9, "bold"), relief="flat", command=self.add_new_game).pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        tk.Button(btn_row, text="- Remover", bg="#ef4444", fg="#ffffff", font=("Segoe UI", 9, "bold"), relief="flat", command=self.remove_game).pack(side=tk.RIGHT, expand=True, fill=tk.X, padx=(4, 0))

        # Painel de Edição (Direita)
        right = tk.Frame(self.tab_games, bg="#18181c", highlightthickness=1, highlightbackground="#2d2d35")
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(10, 10), pady=15)

        tk.Label(right, text="Nome do Jogo:", font=("Segoe UI", 9, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=20, pady=(15, 2))
        self.entry_name = tk.Entry(right, bg="#26262e", fg="#ffffff", font=("Segoe UI", 10), insertbackground="#ffffff", relief="flat")
        self.entry_name.pack(fill=tk.X, padx=20, pady=(0, 10))

        tk.Label(right, text="Nome do Executável (.exe):", font=("Segoe UI", 9, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=20, pady=(5, 2))
        exe_row = tk.Frame(right, bg="#18181c")
        exe_row.pack(fill=tk.X, padx=20, pady=(0, 15))
        self.entry_exe = tk.Entry(exe_row, bg="#26262e", fg="#ffffff", font=("Segoe UI", 10), insertbackground="#ffffff", relief="flat")
        self.entry_exe.pack(side=tk.LEFT, fill=tk.X, expand=True)
        tk.Button(exe_row, text="📁 Procurar", bg="#3b82f6", fg="#ffffff", font=("Segoe UI", 9), relief="flat", command=self.browse_exe).pack(side=tk.RIGHT, padx=(6, 0))

        # Slider Saturação
        tk.Label(right, text="Saturação no Jogo (Digital Vibrance):", font=("Segoe UI", 9, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=20, pady=(5, 2))
        self.lbl_game_vib = tk.Label(right, text="100%", font=("Segoe UI", 11, "bold"), fg="#60a5fa", bg="#18181c")
        self.lbl_game_vib.pack(anchor="e", padx=25, pady=(0, 0))
        self.slider_game_vib = tk.Scale(right, from_=50, to=100, orient=tk.HORIZONTAL, bg="#18181c", fg="#ffffff", highlightthickness=0, troughcolor="#26262e", activebackground="#3b82f6", command=lambda v: self.lbl_game_vib.config(text=f"{v}%"))
        self.slider_game_vib.pack(fill=tk.X, padx=20, pady=(0, 15))

        # Slider Nitidez
        tk.Label(right, text="Nitidez no Jogo (Image Sharpening):", font=("Segoe UI", 9, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=20, pady=(5, 2))
        self.lbl_game_sharp = tk.Label(right, text="0%", font=("Segoe UI", 11, "bold"), fg="#a78bfa", bg="#18181c")
        self.lbl_game_sharp.pack(anchor="e", padx=25, pady=(0, 0))
        self.slider_game_sharp = tk.Scale(right, from_=0, to=100, orient=tk.HORIZONTAL, bg="#18181c", fg="#ffffff", highlightthickness=0, troughcolor="#26262e", activebackground="#8b5cf6", command=lambda v: self.lbl_game_sharp.config(text=f"{v}%"))
        self.slider_game_sharp.pack(fill=tk.X, padx=20, pady=(0, 15))

        self.var_windowed = tk.BooleanVar(value=True)
        tk.Checkbutton(right, text="Ativar também em Modo Janela / Janela Sem Bordas", variable=self.var_windowed, bg="#18181c", fg="#ffffff", selectcolor="#26262e", activebackground="#18181c", activeforeground="#ffffff").pack(anchor="w", padx=20, pady=4)

        self.var_enabled = tk.BooleanVar(value=True)
        tk.Checkbutton(right, text="Perfil Habilitado", variable=self.var_enabled, bg="#18181c", fg="#ffffff", selectcolor="#26262e", activebackground="#18181c", activeforeground="#ffffff").pack(anchor="w", padx=20, pady=4)

        tk.Button(right, text="💾 Salvar Alterações do Perfil", bg="#3b82f6", fg="#ffffff", font=("Segoe UI", 10, "bold"), relief="flat", command=self.save_current_profile).pack(fill=tk.X, padx=20, pady=20)

        self.populate_listbox()

    def setup_tab_test(self):
        box = tk.Frame(self.tab_test, bg="#18181c", highlightthickness=1, highlightbackground="#2d2d35")
        box.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)

        tk.Label(box, text="Teste em Tempo Real no Monitor", font=("Segoe UI", 13, "bold"), fg="#ffffff", bg="#18181c").pack(anchor="w", padx=25, pady=(20, 5))
        tk.Label(box, text="Arraste os controles abaixo para ver as cores e a nitidez mudando diretamente na sua tela agora mesmo.", font=("Segoe UI", 9), fg="#9ca3af", bg="#18181c").pack(anchor="w", padx=25, pady=(0, 20))

        tk.Label(box, text="Saturação para Teste (0% a 100%):", font=("Segoe UI", 10, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=25)
        self.lbl_test_vib = tk.Label(box, text="100%", font=("Segoe UI", 12, "bold"), fg="#60a5fa", bg="#18181c")
        self.lbl_test_vib.pack(anchor="e", padx=30)
        self.slider_test_vib = tk.Scale(box, from_=0, to=100, orient=tk.HORIZONTAL, bg="#18181c", fg="#ffffff", highlightthickness=0, troughcolor="#26262e", activebackground="#3b82f6", command=self.on_live_vib_change)
        self.slider_test_vib.set(100)
        self.slider_test_vib.pack(fill=tk.X, padx=25, pady=(0, 20))

        tk.Label(box, text="Nitidez para Teste (0% a 100%):", font=("Segoe UI", 10, "bold"), fg="#d1d5db", bg="#18181c").pack(anchor="w", padx=25)
        self.lbl_test_sharp = tk.Label(box, text="0%", font=("Segoe UI", 12, "bold"), fg="#a78bfa", bg="#18181c")
        self.lbl_test_sharp.pack(anchor="e", padx=30)
        self.slider_test_sharp = tk.Scale(box, from_=0, to=100, orient=tk.HORIZONTAL, bg="#18181c", fg="#ffffff", highlightthickness=0, troughcolor="#26262e", activebackground="#8b5cf6", command=self.on_live_sharp_change)
        self.slider_test_sharp.set(0)
        self.slider_test_sharp.pack(fill=tk.X, padx=25, pady=(0, 25))

        btn_box = tk.Frame(box, bg="#18181c")
        btn_box.pack(fill=tk.X, padx=25, pady=10)
        tk.Button(btn_box, text="🔄 Restaurar Padrão Windows (50%)", bg="#374151", fg="#ffffff", font=("Segoe UI", 10, "bold"), relief="flat", command=self.restore_desktop).pack(fill=tk.X, pady=5)

    def setup_tab_guide(self):
        container = tk.Frame(self.tab_guide, bg="#121214")
        container.pack(fill=tk.BOTH, expand=True, padx=20, pady=15)

        cards = [
            ("⚡ O que é Digital Vibrance?",
             "Digital Vibrance ajusta o processador de cores interno da GPU NVIDIA antes do sinal ir para o monitor. Em jogos de tiro competitivos, faz modelos de jogadores, silhuetas e miras se destacarem fortemente de fundos escuros, sem reduzir FPS.",
             "#3b82f6"),
            ("🛡️ 100% Seguro Contra Anti-Cheat (VAC, Vanguard, EAC, BattlEye)",
             "Este utilitário é 100% externo. Ele JAMAIS injeta DLLs, não altera arquivos e não lê a memória de nenhum jogo. Ele opera exclusivamente através da API oficial do driver NVIDIA (NVAPI), exatamente igual ao Painel de Controle da NVIDIA.",
             "#22c55e"),
            ("🔍 Nitidez (Image Sharpening)",
             "Realça bordas e contornos de objetos distantes. É excelente para quem joga em resoluções esticadas (como 4:3 1280x960 no CS2) onde a imagem pode ficar embaçada devido ao esticamento do monitor.",
             "#8b5cf6"),
            ("🔄 Como funciona a Troca Automática?",
             "O software observa passivamente qual janela está em primeiro plano no Windows. Ao focar no jogo, a saturação sobe automaticamente para o nível configurado. Ao dar Alt+Tab ou fechar o jogo, a saturação volta aos 50% naturais da área de trabalho.",
             "#eab308")
        ]

        for title, text, color in cards:
            card = tk.Frame(container, bg="#18181c", highlightthickness=1, highlightbackground="#2d2d35")
            card.pack(fill=tk.X, pady=6)
            tk.Label(card, text=title, font=("Segoe UI", 10, "bold"), fg=color, bg="#18181c").pack(anchor="w", padx=15, pady=(8, 2))
            tk.Label(card, text=text, font=("Segoe UI", 9), fg="#9ca3af", bg="#18181c", wraplength=730, justify="left").pack(anchor="w", padx=15, pady=(0, 8))

    def populate_listbox(self):
        self.listbox.delete(0, tk.END)
        for p in self.profiles:
            status = "✓" if p.get("enabled", True) else "✗"
            self.listbox.insert(tk.END, f"{status} {p['name']} ({p['vibrance']}%)")
        if self.profiles:
            self.listbox.select_set(0)
            self.load_profile_inputs(0)

    def on_game_selected(self, event):
        sel = self.listbox.curselection()
        if sel:
            self.load_profile_inputs(sel[0])

    def load_profile_inputs(self, idx):
        if idx < 0 or idx >= len(self.profiles):
            return
        p = self.profiles[idx]
        self.entry_name.delete(0, tk.END)
        self.entry_name.insert(0, p.get("name", ""))
        self.entry_exe.delete(0, tk.END)
        self.entry_exe.insert(0, p.get("exe", ""))
        self.slider_game_vib.set(p.get("vibrance", 100))
        self.slider_game_sharp.set(p.get("sharpness", 0))
        self.var_windowed.set(p.get("windowed", True))
        self.var_enabled.set(p.get("enabled", True))

    def save_current_profile(self):
        sel = self.listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        self.profiles[idx] = {
            "name": self.entry_name.get().strip(),
            "exe": self.entry_exe.get().strip().lower(),
            "vibrance": int(self.slider_game_vib.get()),
            "sharpness": int(self.slider_game_sharp.get()),
            "windowed": self.var_windowed.get(),
            "enabled": self.var_enabled.get()
        }
        self.data["profiles"] = self.profiles
        save_data(self.data)
        self.populate_listbox()
        self.listbox.select_set(idx)
        messagebox.showinfo("Sucesso", "Perfil atualizado com sucesso!")

    def add_new_game(self):
        new_item = {"name": "Novo Jogo", "exe": "game.exe", "vibrance": 100, "sharpness": 0, "windowed": True, "enabled": True}
        self.profiles.append(new_item)
        self.data["profiles"] = self.profiles
        save_data(self.data)
        self.populate_listbox()
        self.listbox.select_set(len(self.profiles) - 1)
        self.load_profile_inputs(len(self.profiles) - 1)

    def remove_game(self):
        sel = self.listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        del self.profiles[idx]
        self.data["profiles"] = self.profiles
        save_data(self.data)
        self.populate_listbox()

    def browse_exe(self):
        path = filedialog.askopenfilename(filetypes=[("Executáveis", "*.exe")])
        if path:
            exe = os.path.basename(path).lower()
            self.entry_exe.delete(0, tk.END)
            self.entry_exe.insert(0, exe)
            if not self.entry_name.get() or self.entry_name.get() == "Novo Jogo":
                base = os.path.splitext(exe)[0].capitalize()
                self.entry_name.delete(0, tk.END)
                self.entry_name.insert(0, base)

    def on_live_vib_change(self, val):
        self.lbl_test_vib.config(text=f"{val}%")
        apply_vibrance(int(val))

    def on_live_sharp_change(self, val):
        self.lbl_test_sharp.config(text=f"{val}%")
        apply_sharpness(int(val))

    def restore_desktop(self):
        apply_vibrance(self.win_vibrance)
        apply_sharpness(0)
        self.slider_test_vib.set(self.win_vibrance)
        self.slider_test_sharp.set(0)

    def start_monitor(self):
        def loop():
            while True:
                time.sleep(0.5)
                proc = get_foreground_process()
                if not proc:
                    continue

                matched = None
                for p in self.profiles:
                    if p.get("enabled", True) and p.get("exe", "").lower() == proc:
                        matched = p
                        break

                if matched:
                    if self.active_profile != matched:
                        apply_vibrance(matched.get("vibrance", 100))
                        apply_sharpness(matched.get("sharpness", 0))
                        self.active_profile = matched
                        self.root.after(0, lambda m=matched: self.lbl_status.config(
                            text=f"🎮 JOGO ATIVO: {m['name']} ({m['vibrance']}%)", fg="#3b82f6"))
                else:
                    if self.active_profile is not None:
                        apply_vibrance(self.win_vibrance)
                        apply_sharpness(0)
                        self.active_profile = None
                        self.root.after(0, lambda: self.lbl_status.config(
                            text=f"🟢 STATUS: Em Espera (Windows: {self.win_vibrance}%)", fg="#22c55e"))

        t = threading.Thread(target=loop, daemon=True)
        t.start()

if __name__ == "__main__":
    root = tk.Tk()
    app = VibranceAppGUI(root)
    root.mainloop()

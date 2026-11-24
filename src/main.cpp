#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <cwctype>
#include <cstdio>

// --- Definições NVAPI ---
#define NVAPI_INITIALIZE_ID               0x0150E828
#define NVAPI_UNLOAD_ID                   0xD22BDD7E
#define NVAPI_ENUM_DISPLAY_HANDLE_ID      0x9ABDD40D
#define NVAPI_GET_DVC_INFO_ID             0x4085DE45
#define NVAPI_SET_DVC_LEVEL_ID            0x172409B4
#define NVAPI_OVERLAY_SHARPNESS_ID        0x7A2BC2B2
#define NVAPI_GET_ERROR_MSG_ID            0x6C2D048C

#pragma pack(push, 8)
struct NV_DISPLAY_DVC_INFO {
    unsigned int version;
    int currentLevel;
    int minLevel;
    int maxLevel;
};
#pragma pack(pop)

typedef void* (*NvAPI_QueryInterface_t)(unsigned int offset);
typedef int (*NvAPI_Initialize_t)(void);
typedef int (*NvAPI_Unload_t)(void);
typedef int (*NvAPI_EnumNvidiaDisplayHandle_t)(int index, void** pHandle);
typedef int (*NvAPI_GetDVCInfo_t)(void* hDisplay, int outputId, NV_DISPLAY_DVC_INFO* pInfo);
typedef int (*NvAPI_SetDVCLevel_t)(void* hDisplay, int outputId, int level);
typedef int (*NvAPI_GetErrorMessage_t)(int status, char* szDesc);
typedef int (*NvAPI_OverlaySharpness_t)(void* hDisplay, int level);

static NvAPI_Initialize_t             g_NvAPI_Initialize = nullptr;
static NvAPI_Unload_t                 g_NvAPI_Unload = nullptr;
static NvAPI_EnumNvidiaDisplayHandle_t g_NvAPI_EnumDisplay = nullptr;
static NvAPI_GetDVCInfo_t             g_NvAPI_GetDVCInfo = nullptr;
static NvAPI_SetDVCLevel_t            g_NvAPI_SetDVCLevel = nullptr;
static NvAPI_OverlaySharpness_t       g_NvAPI_OverlaySharpness = nullptr;
static HMODULE                        g_hNvApiDll = nullptr;
static std::vector<void*>             g_displayHandles;

// --- Configuração ---
struct AppConfig {
    int windowsVibrance = 50;
    int cs2Vibrance = 100;
    int sharpnessPercent = 0;
    bool applyInWindowed = true;
    int pollingIntervalMs = 500;
    std::wstring targetProcess = L"cs2.exe";
} g_config;

// --- Variáveis de Estado ---
static const UINT WM_APP_TRAY = WM_APP + 101;
static const UINT_PTR IDT_CHECK_TIMER = 1001;
static NOTIFYICONDATAW g_nid = {};
static HWND g_hWnd = nullptr;
static HANDLE g_hMutex = nullptr;
static bool g_isGameActive = false;
static bool g_nvapiReady = false;

// --- Identificadores de Menu ---
enum MenuCommands {
    IDM_STATUS = 2000,
    IDM_CS2_VIB_100,
    IDM_CS2_VIB_85,
    IDM_CS2_VIB_75,
    IDM_CS2_VIB_50,
    IDM_WIN_VIB_50,
    IDM_SHARP_0,
    IDM_SHARP_25,
    IDM_SHARP_50,
    IDM_SHARP_75,
    IDM_SHARP_100,
    IDM_WINDOWED,
    IDM_AUTOSTART,
    IDM_FORCE_CS2,
    IDM_FORCE_WIN,
    IDM_OPEN_CONFIG,
    IDM_EXIT
};

// --- Funções Auxiliares de Configuração (INI) ---
std::wstring GetConfigPath() {
    WCHAR appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\CS2Vibrance";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\config.ini";
    }
    return L"config.ini";
}

void LoadConfiguration() {
    std::wstring path = GetConfigPath();
    g_config.windowsVibrance = GetPrivateProfileIntW(L"Settings", L"WindowsVibrance", 50, path.c_str());
    g_config.cs2Vibrance = GetPrivateProfileIntW(L"Settings", L"CS2Vibrance", 100, path.c_str());
    g_config.sharpnessPercent = GetPrivateProfileIntW(L"Settings", L"SharpnessPercent", 0, path.c_str());
    g_config.applyInWindowed = (GetPrivateProfileIntW(L"Settings", L"ApplyInWindowed", 1, path.c_str()) != 0);
    g_config.pollingIntervalMs = GetPrivateProfileIntW(L"Settings", L"PollingIntervalMs", 500, path.c_str());

    WCHAR target[128] = {0};
    GetPrivateProfileStringW(L"Settings", L"TargetProcess", L"cs2.exe", target, 128, path.c_str());
    g_config.targetProcess = target;
    for (auto& c : g_config.targetProcess) c = towlower(c);
}

void SaveConfiguration() {
    std::wstring path = GetConfigPath();
    WritePrivateProfileStringW(L"Settings", L"WindowsVibrance", std::to_wstring(g_config.windowsVibrance).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"CS2Vibrance", std::to_wstring(g_config.cs2Vibrance).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"SharpnessPercent", std::to_wstring(g_config.sharpnessPercent).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"ApplyInWindowed", g_config.applyInWindowed ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"PollingIntervalMs", std::to_wstring(g_config.pollingIntervalMs).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"TargetProcess", g_config.targetProcess.c_str(), path.c_str());
}

// --- Autostart no Registro ---
bool IsAutostartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR value[MAX_PATH];
        DWORD size = sizeof(value);
        LONG res = RegQueryValueExW(hKey, L"CS2Vibrance", NULL, NULL, (LPBYTE)value, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void SetAutostart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"CS2Vibrance", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"CS2Vibrance");
        }
        RegCloseKey(hKey);
    }
}

// --- Inicialização do NVAPI ---
bool InitNVAPI() {
    g_hNvApiDll = LoadLibraryW(L"nvapi64.dll");
    if (!g_hNvApiDll) {
        g_hNvApiDll = LoadLibraryW(L"nvapi.dll");
    }
    if (!g_hNvApiDll) return false;

    NvAPI_QueryInterface_t pQueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(g_hNvApiDll, "nvapi_QueryInterface");
    if (!pQueryInterface) return false;

    g_NvAPI_Initialize = (NvAPI_Initialize_t)pQueryInterface(NVAPI_INITIALIZE_ID);
    g_NvAPI_Unload = (NvAPI_Unload_t)pQueryInterface(NVAPI_UNLOAD_ID);
    g_NvAPI_EnumDisplay = (NvAPI_EnumNvidiaDisplayHandle_t)pQueryInterface(NVAPI_ENUM_DISPLAY_HANDLE_ID);
    g_NvAPI_GetDVCInfo = (NvAPI_GetDVCInfo_t)pQueryInterface(NVAPI_GET_DVC_INFO_ID);
    g_NvAPI_SetDVCLevel = (NvAPI_SetDVCLevel_t)pQueryInterface(NVAPI_SET_DVC_LEVEL_ID);
    g_NvAPI_OverlaySharpness = (NvAPI_OverlaySharpness_t)pQueryInterface(NVAPI_OVERLAY_SHARPNESS_ID);

    if (!g_NvAPI_Initialize || !g_NvAPI_EnumDisplay || !g_NvAPI_SetDVCLevel) {
        return false;
    }

    if (g_NvAPI_Initialize() != 0) {
        return false;
    }

    // Enumerar todos os monitores NVIDIA ativos
    g_displayHandles.clear();
    for (int i = 0; i < 16; i++) {
        void* hDisplay = nullptr;
        if (g_NvAPI_EnumDisplay(i, &hDisplay) == 0 && hDisplay != nullptr) {
            g_displayHandles.push_back(hDisplay);
        }
    }

    g_nvapiReady = (!g_displayHandles.empty());
    return g_nvapiReady;
}

void UnloadNVAPI() {
    if (g_NvAPI_Unload) {
        g_NvAPI_Unload();
    }
    if (g_hNvApiDll) {
        FreeLibrary(g_hNvApiDll);
        g_hNvApiDll = nullptr;
    }
}

// --- Aplicação de Vibrance e Nitidez ---
void ApplyVibrancePercent(int percent) {
    if (!g_nvapiReady) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    for (void* hDisplay : g_displayHandles) {
        NV_DISPLAY_DVC_INFO info = {};
        info.version = (unsigned int)(sizeof(NV_DISPLAY_DVC_INFO) | 0x10000);
        int minLvl = 0, maxLvl = 63;
        if (g_NvAPI_GetDVCInfo && g_NvAPI_GetDVCInfo(hDisplay, 0, &info) == 0) {
            minLvl = info.minLevel;
            maxLvl = info.maxLevel;
        }
        int level = minLvl + (int)((float)(maxLvl - minLvl) * (percent / 100.0f) + 0.5f);
        g_NvAPI_SetDVCLevel(hDisplay, 0, level);
    }
}

void ApplySharpnessPercent(int percent) {
    if (!g_nvapiReady || !g_NvAPI_OverlaySharpness) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    for (void* hDisplay : g_displayHandles) {
        g_NvAPI_OverlaySharpness(hDisplay, percent);
    }
}

// --- Atualização de Tooltip na Bandeja ---
void UpdateTrayTooltip(const std::wstring& status) {
    std::wstring tip = L"CS2 Vibrance: " + status;
    wcsncpy(g_nid.szTip, tip.c_str(), sizeof(g_nid.szTip) / sizeof(WCHAR) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// --- Verificação de Processo em Primeiro Plano e Tela Cheia ---
std::wstring GetForegroundProcessName(HWND* outHwnd) {
    HWND hwnd = GetForegroundWindow();
    if (outHwnd) *outHwnd = hwnd;
    if (!hwnd) return L"";

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return L"";

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"";

    WCHAR fullPath[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    std::wstring procName = L"";
    if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
        std::wstring s(fullPath);
        size_t pos = s.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            procName = s.substr(pos + 1);
        } else {
            procName = s;
        }
        for (auto& c : procName) c = towlower(c);
    }
    CloseHandle(hProc);
    return procName;
}

bool IsWindowFullscreen(HWND hwnd) {
    if (!hwnd) return false;
    RECT rc;
    if (GetWindowRect(hwnd, &rc)) {
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi)) {
            int monW = mi.rcMonitor.right - mi.rcMonitor.left;
            int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            return (winW >= monW && winH >= monH);
        }
    }
    return true;
}

// --- Ciclo de Verificação de Foco (Timer) ---
void CheckForegroundGame() {
    HWND fgHwnd = nullptr;
    std::wstring procName = GetForegroundProcessName(&fgHwnd);

    bool matches = (procName == g_config.targetProcess);
    if (matches && !g_config.applyInWindowed) {
        matches = IsWindowFullscreen(fgHwnd);
    }

    if (matches) {
        if (!g_isGameActive) {
            ApplyVibrancePercent(g_config.cs2Vibrance);
            if (g_config.sharpnessPercent > 0) {
                ApplySharpnessPercent(g_config.sharpnessPercent);
            }
            g_isGameActive = true;
            UpdateTrayTooltip(L"ATIVO (" + std::to_wstring(g_config.cs2Vibrance) + L"%)");
        }
    } else {
        if (g_isGameActive) {
            ApplyVibrancePercent(g_config.windowsVibrance);
            if (g_config.sharpnessPercent > 0) {
                ApplySharpnessPercent(0);
            }
            g_isGameActive = false;
            UpdateTrayTooltip(L"Standby (" + std::to_wstring(g_config.windowsVibrance) + L"%)");
        }
    }
}

// --- Menu de Contexto do Tray Icon ---
void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    std::wstring statusStr = g_isGameActive ? L"Status: CS2 ATIVO" : L"Status: Em Espera (CS2)";
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_STATUS, statusStr.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    // Submenu CS2 Vibrance
    HMENU hCs2Menu = CreatePopupMenu();
    AppendMenuW(hCs2Menu, MF_STRING | (g_config.cs2Vibrance == 100 ? MF_CHECKED : 0), IDM_CS2_VIB_100, L"100% (Competitivo)");
    AppendMenuW(hCs2Menu, MF_STRING | (g_config.cs2Vibrance == 85 ? MF_CHECKED : 0), IDM_CS2_VIB_85, L"85%");
    AppendMenuW(hCs2Menu, MF_STRING | (g_config.cs2Vibrance == 75 ? MF_CHECKED : 0), IDM_CS2_VIB_75, L"75%");
    AppendMenuW(hCs2Menu, MF_STRING | (g_config.cs2Vibrance == 50 ? MF_CHECKED : 0), IDM_CS2_VIB_50, L"50% (Padrão)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCs2Menu, L"CS2 Vibrance");

    // Submenu Windows Vibrance
    HMENU hWinMenu = CreatePopupMenu();
    AppendMenuW(hWinMenu, MF_STRING | (g_config.windowsVibrance == 50 ? MF_CHECKED : 0), IDM_WIN_VIB_50, L"50% (Padrão)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hWinMenu, L"Windows Vibrance");

    // Submenu Nitidez (Sharpness)
    HMENU hSharpMenu = CreatePopupMenu();
    AppendMenuW(hSharpMenu, MF_STRING | (g_config.sharpnessPercent == 0 ? MF_CHECKED : 0), IDM_SHARP_0, L"0% (Desativada)");
    AppendMenuW(hSharpMenu, MF_STRING | (g_config.sharpnessPercent == 25 ? MF_CHECKED : 0), IDM_SHARP_25, L"25%");
    AppendMenuW(hSharpMenu, MF_STRING | (g_config.sharpnessPercent == 50 ? MF_CHECKED : 0), IDM_SHARP_50, L"50%");
    AppendMenuW(hSharpMenu, MF_STRING | (g_config.sharpnessPercent == 75 ? MF_CHECKED : 0), IDM_SHARP_75, L"75%");
    AppendMenuW(hSharpMenu, MF_STRING | (g_config.sharpnessPercent == 100 ? MF_CHECKED : 0), IDM_SHARP_100, L"100%");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSharpMenu, L"Nitidez (Sharpness)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | (g_config.applyInWindowed ? MF_CHECKED : 0), IDM_WINDOWED, L"Ativar também em Modo Janela");
    AppendMenuW(hMenu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART, L"Iniciar com o Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_FORCE_CS2, L"Forçar Perfil CS2 Agora");
    AppendMenuW(hMenu, MF_STRING, IDM_FORCE_WIN, L"Forçar Padrão Windows Agora");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_CONFIG, L"Abrir Configurações (config.ini)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Sair");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// --- Tratamento de Mensagens da Janela Oculta ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, IDT_CHECK_TIMER, g_config.pollingIntervalMs, NULL);
            break;

        case WM_TIMER:
            if (wParam == IDT_CHECK_TIMER) {
                CheckForegroundGame();
            }
            break;

        case WM_APP_TRAY:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowContextMenu(hwnd, pt);
            }
            break;

        case WM_COMMAND: {
            int cmdId = LOWORD(wParam);
            switch (cmdId) {
                case IDM_CS2_VIB_100: g_config.cs2Vibrance = 100; SaveConfiguration(); if (g_isGameActive) ApplyVibrancePercent(100); break;
                case IDM_CS2_VIB_85:  g_config.cs2Vibrance = 85;  SaveConfiguration(); if (g_isGameActive) ApplyVibrancePercent(85); break;
                case IDM_CS2_VIB_75:  g_config.cs2Vibrance = 75;  SaveConfiguration(); if (g_isGameActive) ApplyVibrancePercent(75); break;
                case IDM_CS2_VIB_50:  g_config.cs2Vibrance = 50;  SaveConfiguration(); if (g_isGameActive) ApplyVibrancePercent(50); break;

                case IDM_WIN_VIB_50:  g_config.windowsVibrance = 50; SaveConfiguration(); if (!g_isGameActive) ApplyVibrancePercent(50); break;

                case IDM_SHARP_0:   g_config.sharpnessPercent = 0;   SaveConfiguration(); ApplySharpnessPercent(0); break;
                case IDM_SHARP_25:  g_config.sharpnessPercent = 25;  SaveConfiguration(); if (g_isGameActive) ApplySharpnessPercent(25); break;
                case IDM_SHARP_50:  g_config.sharpnessPercent = 50;  SaveConfiguration(); if (g_isGameActive) ApplySharpnessPercent(50); break;
                case IDM_SHARP_75:  g_config.sharpnessPercent = 75;  SaveConfiguration(); if (g_isGameActive) ApplySharpnessPercent(75); break;
                case IDM_SHARP_100: g_config.sharpnessPercent = 100; SaveConfiguration(); if (g_isGameActive) ApplySharpnessPercent(100); break;

                case IDM_WINDOWED:
                    g_config.applyInWindowed = !g_config.applyInWindowed;
                    SaveConfiguration();
                    break;

                case IDM_AUTOSTART:
                    SetAutostart(!IsAutostartEnabled());
                    break;

                case IDM_FORCE_CS2:
                    ApplyVibrancePercent(g_config.cs2Vibrance);
                    if (g_config.sharpnessPercent > 0) ApplySharpnessPercent(g_config.sharpnessPercent);
                    g_isGameActive = true;
                    UpdateTrayTooltip(L"FORÇADO CS2 (" + std::to_wstring(g_config.cs2Vibrance) + L"%)");
                    break;

                case IDM_FORCE_WIN:
                    ApplyVibrancePercent(g_config.windowsVibrance);
                    ApplySharpnessPercent(0);
                    g_isGameActive = false;
                    UpdateTrayTooltip(L"FORÇADO Windows (" + std::to_wstring(g_config.windowsVibrance) + L"%)");
                    break;

                case IDM_OPEN_CONFIG:
                    ShellExecuteW(NULL, L"open", GetConfigPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;

                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;
        }

        case WM_DESTROY:
            KillTimer(hwnd, IDT_CHECK_TIMER);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            ApplyVibrancePercent(g_config.windowsVibrance);
            ApplySharpnessPercent(0);
            UnloadNVAPI();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Ponto de Entrada WinMain ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Instância única via Mutex
    g_hMutex = CreateMutexW(NULL, TRUE, L"CS2Vibrance~Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"O CS2 Vibrance já está em execução na bandeja do sistema.", L"CS2 Vibrance", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LoadConfiguration();

    if (!InitNVAPI()) {
        MessageBoxW(NULL, L"Não foi possível inicializar a API NVIDIA (NVAPI).\nVerifique se o driver NVIDIA está instalado corretamente.", L"Erro NVAPI", MB_OK | MB_ICONERROR);
        if (g_hMutex) CloseHandle(g_hMutex);
        return 1;
    }

    // Criar classe de janela invisível para mensagens
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CS2VibranceHiddenWindowClass";
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"CS2VibranceHiddenWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (!g_hWnd) {
        UnloadNVAPI();
        if (g_hMutex) CloseHandle(g_hMutex);
        return 1;
    }

    // Configurar ícone na bandeja do sistema
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcsncpy(g_nid.szTip, L"CS2 Vibrance: Standby (50%)", sizeof(g_nid.szTip) / sizeof(WCHAR) - 1);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Loop de mensagens Win32
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
    }

    return (int)msg.wParam;
}

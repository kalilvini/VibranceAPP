#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <cwctype>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

// --- Definições NVAPI ---
#define NVAPI_INITIALIZE_ID               0x0150E828
#define NVAPI_UNLOAD_ID                   0xD22BDD7E
#define NVAPI_ENUM_DISPLAY_HANDLE_ID      0x9ABDD40D
#define NVAPI_GET_DVC_INFO_ID             0x4085DE45
#define NVAPI_SET_DVC_LEVEL_ID            0x172409B4
#define NVAPI_OVERLAY_SHARPNESS_ID        0x7A2BC2B2
#define NVAPI_ENUM_PHYSICAL_GPUS_ID       0xE5AC921F
#define NVAPI_GPU_GET_FULL_NAME_ID        0xCEEE8E9F

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
typedef int (*NvAPI_OverlaySharpness_t)(void* hDisplay, int level);
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** gpuHandles, int* pGpuCount);
typedef int (*NvAPI_GPU_GetFullName_t)(void* hPhysicalGpu, char* szName);

static NvAPI_Initialize_t             g_NvAPI_Initialize = nullptr;
static NvAPI_Unload_t                 g_NvAPI_Unload = nullptr;
static NvAPI_EnumNvidiaDisplayHandle_t g_NvAPI_EnumDisplay = nullptr;
static NvAPI_GetDVCInfo_t             g_NvAPI_GetDVCInfo = nullptr;
static NvAPI_SetDVCLevel_t            g_NvAPI_SetDVCLevel = nullptr;
static NvAPI_OverlaySharpness_t       g_NvAPI_OverlaySharpness = nullptr;
static NvAPI_EnumPhysicalGPUs_t       g_NvAPI_EnumGPUs = nullptr;
static NvAPI_GPU_GetFullName_t        g_NvAPI_GetGpuName = nullptr;

static HMODULE                        g_hNvApiDll = nullptr;
static std::vector<void*>             g_displayHandles;
static std::wstring                   g_gpuName = L"NVIDIA GeForce (Detectando...)";
static bool                           g_nvapiReady = false;

// --- Estrutura de Perfis de Jogos ---
struct GameProfile {
    std::wstring name;
    std::wstring exeName;
    int vibrance;       // 0 a 100%
    int sharpness;      // 0 a 100%
    bool applyWindowed; // true: modo janela + fullscreen, false: fullscreen apenas
    bool enabled;
};

struct AppSettings {
    int windowsVibrance = 50;
    bool startWithWindows = false;
    bool minimizeToTrayOnClose = true;
    int pollingIntervalMs = 500;
    std::vector<GameProfile> profiles;
} g_settings;

// --- Variáveis de Estado da Janela e GUI ---
static const UINT WM_APP_TRAY = WM_APP + 201;
static const UINT_PTR IDT_POLL_TIMER = 3001;

static HWND g_hMainWindow = nullptr;
static NOTIFYICONDATAW g_nid = {};
static HANDLE g_hMutex = nullptr;

// Fontes
static HFONT g_fontTitle = nullptr;
static HFONT g_fontTab = nullptr;
static HFONT g_fontBold = nullptr;
static HFONT g_fontRegular = nullptr;
static HFONT g_fontSmall = nullptr;

// Pincéis de Cores Dark Mode
static HBRUSH g_hBrushBg = nullptr;      // #121216 (Fundo Principal)
static HBRUSH g_hBrushCard = nullptr;    // #1a1a22 (Cards e Painéis)
static HBRUSH g_hBrushInput = nullptr;   // #242430 (Campos de Entrada)
static HBRUSH g_hBrushHover = nullptr;   // #2e2e3c (Destaques)

static bool g_isGameActive = false;
static std::wstring g_activeGameName = L"";
static int g_currentTab = 0;
static int g_selectedProfileIndex = 0;

// IDs dos Controles da GUI
enum ControlIDs {
    IDC_STATUS_LABEL = 100,
    IDC_GPU_LABEL,

    // Botões de Navegação (Abas Customizadas)
    IDC_TAB_BTN_0 = 150,
    IDC_TAB_BTN_1,
    IDC_TAB_BTN_2,
    IDC_TAB_BTN_3,

    // Tab 1: Jogos & Perfis
    IDC_PROFILE_LIST = 200,
    IDC_BTN_ADD_GAME,
    IDC_BTN_REMOVE_GAME,
    IDC_BTN_DETECT_RUNNING,
    IDC_BTN_BROWSE_EXE,
    IDC_BTN_SAVE_PROFILES,
    IDC_EDIT_GAME_NAME,
    IDC_EDIT_GAME_EXE,
    IDC_SLIDER_GAME_VIB,
    IDC_LBL_GAME_VIB_VAL,
    IDC_SLIDER_GAME_SHARP,
    IDC_LBL_GAME_SHARP_VAL,
    IDC_CHK_WINDOWED,
    IDC_CHK_PROFILE_ENABLED,

    // Tab 2: Teste Rápido / Ao Vivo
    IDC_SLIDER_TEST_VIB = 300,
    IDC_LBL_TEST_VIB_VAL,
    IDC_SLIDER_TEST_SHARP,
    IDC_LBL_TEST_SHARP_VAL,
    IDC_BTN_APPLY_TEST,
    IDC_BTN_RESTORE_DEFAULT,

    // Tab 3: Configurações Gerais
    IDC_SLIDER_WIN_VIB = 400,
    IDC_LBL_WIN_VIB_VAL,
    IDC_CHK_AUTOSTART,
    IDC_CHK_MINIMIZE_TRAY,
    IDC_COMBO_INTERVAL,
    IDC_BTN_SAVE_SETTINGS,

    // Tray Menu
    IDM_TRAY_OPEN = 2000,
    IDM_TRAY_STATUS,
    IDM_TRAY_RESTORE_WIN,
    IDM_TRAY_EXIT
};

// Controles por aba para exibição dinâmica
static std::vector<HWND> g_tab0Controls;
static std::vector<HWND> g_tab1Controls;
static std::vector<HWND> g_tab2Controls;
static std::vector<HWND> g_tab3Controls;
static HWND g_tabButtons[4];

// --- Persistência de Dados (INI) ---
std::wstring GetConfigDir() {
    WCHAR appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\VibranceAPP";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir;
    }
    return L".";
}

std::wstring GetProfilesFilePath() {
    return GetConfigDir() + L"\\profiles.ini";
}

void SetDefaultProfiles() {
    g_settings.profiles.clear();
    g_settings.profiles.push_back({L"Counter-Strike 2", L"cs2.exe", 100, 0, true, true});
    g_settings.profiles.push_back({L"Valorant", L"valorant-win64-shipping.exe", 85, 0, true, true});
    g_settings.profiles.push_back({L"Apex Legends", L"r5apex.exe", 80, 0, true, true});
    g_settings.profiles.push_back({L"Call of Duty / Warzone", L"cod.exe", 85, 0, true, true});
    g_settings.profiles.push_back({L"Fortnite", L"fortniteclient-win64-shipping.exe", 75, 0, true, true});
    g_settings.profiles.push_back({L"Overwatch 2", L"overwatch.exe", 80, 0, true, true});
    g_settings.profiles.push_back({L"Rainbow Six Siege", L"rainbowsix.exe", 85, 0, true, true});
}

bool IsAutostartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR value[MAX_PATH];
        DWORD size = sizeof(value);
        LONG res = RegQueryValueExW(hKey, L"VibranceAPP", NULL, NULL, (LPBYTE)value, &size);
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
            RegSetValueExW(hKey, L"VibranceAPP", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"VibranceAPP");
        }
        RegCloseKey(hKey);
    }
}

void LoadSettings() {
    std::wstring path = GetProfilesFilePath();
    g_settings.windowsVibrance = GetPrivateProfileIntW(L"General", L"WindowsVibrance", 50, path.c_str());
    g_settings.minimizeToTrayOnClose = (GetPrivateProfileIntW(L"General", L"MinimizeToTray", 1, path.c_str()) != 0);
    g_settings.pollingIntervalMs = GetPrivateProfileIntW(L"General", L"PollingInterval", 500, path.c_str());
    g_settings.startWithWindows = IsAutostartEnabled();

    int count = GetPrivateProfileIntW(L"Profiles", L"Count", -1, path.c_str());
    if (count <= 0) {
        SetDefaultProfiles();
    } else {
        g_settings.profiles.clear();
        for (int i = 0; i < count; i++) {
            std::wstring sec = L"Profile_" + std::to_wstring(i);
            WCHAR name[128] = {0};
            WCHAR exe[128] = {0};
            GetPrivateProfileStringW(sec.c_str(), L"Name", L"Game", name, 128, path.c_str());
            GetPrivateProfileStringW(sec.c_str(), L"Exe", L"game.exe", exe, 128, path.c_str());
            int vib = GetPrivateProfileIntW(sec.c_str(), L"Vibrance", 100, path.c_str());
            int shp = GetPrivateProfileIntW(sec.c_str(), L"Sharpness", 0, path.c_str());
            bool win = (GetPrivateProfileIntW(sec.c_str(), L"Windowed", 1, path.c_str()) != 0);
            bool en = (GetPrivateProfileIntW(sec.c_str(), L"Enabled", 1, path.c_str()) != 0);

            std::wstring exeLower(exe);
            for (auto& c : exeLower) c = towlower(c);
            g_settings.profiles.push_back({name, exeLower, vib, shp, win, en});
        }
    }
}

void SaveSettings() {
    std::wstring path = GetProfilesFilePath();
    WritePrivateProfileStringW(L"General", L"WindowsVibrance", std::to_wstring(g_settings.windowsVibrance).c_str(), path.c_str());
    WritePrivateProfileStringW(L"General", L"MinimizeToTray", g_settings.minimizeToTrayOnClose ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"General", L"PollingInterval", std::to_wstring(g_settings.pollingIntervalMs).c_str(), path.c_str());

    WritePrivateProfileStringW(L"Profiles", L"Count", std::to_wstring(g_settings.profiles.size()).c_str(), path.c_str());
    for (size_t i = 0; i < g_settings.profiles.size(); i++) {
        std::wstring sec = L"Profile_" + std::to_wstring(i);
        WritePrivateProfileStringW(sec.c_str(), L"Name", g_settings.profiles[i].name.c_str(), path.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Exe", g_settings.profiles[i].exeName.c_str(), path.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Vibrance", std::to_wstring(g_settings.profiles[i].vibrance).c_str(), path.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Sharpness", std::to_wstring(g_settings.profiles[i].sharpness).c_str(), path.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Windowed", g_settings.profiles[i].applyWindowed ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(sec.c_str(), L"Enabled", g_settings.profiles[i].enabled ? L"1" : L"0", path.c_str());
    }

    SetAutostart(g_settings.startWithWindows);
}

// --- Funções NVAPI ---
bool InitNVAPI() {
    g_hNvApiDll = LoadLibraryW(L"nvapi64.dll");
    if (!g_hNvApiDll) g_hNvApiDll = LoadLibraryW(L"nvapi.dll");
    if (!g_hNvApiDll) return false;

    NvAPI_QueryInterface_t pQuery = (NvAPI_QueryInterface_t)GetProcAddress(g_hNvApiDll, "nvapi_QueryInterface");
    if (!pQuery) return false;

    g_NvAPI_Initialize = (NvAPI_Initialize_t)pQuery(NVAPI_INITIALIZE_ID);
    g_NvAPI_Unload = (NvAPI_Unload_t)pQuery(NVAPI_UNLOAD_ID);
    g_NvAPI_EnumDisplay = (NvAPI_EnumNvidiaDisplayHandle_t)pQuery(NVAPI_ENUM_DISPLAY_HANDLE_ID);
    g_NvAPI_GetDVCInfo = (NvAPI_GetDVCInfo_t)pQuery(NVAPI_GET_DVC_INFO_ID);
    g_NvAPI_SetDVCLevel = (NvAPI_SetDVCLevel_t)pQuery(NVAPI_SET_DVC_LEVEL_ID);
    g_NvAPI_OverlaySharpness = (NvAPI_OverlaySharpness_t)pQuery(NVAPI_OVERLAY_SHARPNESS_ID);
    g_NvAPI_EnumGPUs = (NvAPI_EnumPhysicalGPUs_t)pQuery(NVAPI_ENUM_PHYSICAL_GPUS_ID);
    g_NvAPI_GetGpuName = (NvAPI_GPU_GetFullName_t)pQuery(NVAPI_GPU_GET_FULL_NAME_ID);

    if (!g_NvAPI_Initialize || !g_NvAPI_EnumDisplay || !g_NvAPI_SetDVCLevel) return false;
    if (g_NvAPI_Initialize() != 0) return false;

    // Detectar Monitores
    g_displayHandles.clear();
    for (int i = 0; i < 16; i++) {
        void* hDisp = nullptr;
        if (g_NvAPI_EnumDisplay(i, &hDisp) == 0 && hDisp != nullptr) {
            g_displayHandles.push_back(hDisp);
        }
    }

    // Detectar Nome da GPU
    if (g_NvAPI_EnumGPUs && g_NvAPI_GetGpuName) {
        void* gpus[8] = {0};
        int gpuCount = 0;
        if (g_NvAPI_EnumGPUs(gpus, &gpuCount) == 0 && gpuCount > 0) {
            char nameBuf[128] = {0};
            if (g_NvAPI_GetGpuName(gpus[0], nameBuf) == 0) {
                int len = MultiByteToWideChar(CP_ACP, 0, nameBuf, -1, NULL, 0);
                if (len > 0) {
                    std::vector<WCHAR> wbuf(len);
                    MultiByteToWideChar(CP_ACP, 0, nameBuf, -1, wbuf.data(), len);
                    g_gpuName = wbuf.data();
                }
            }
        }
    }

    g_nvapiReady = (!g_displayHandles.empty());
    return g_nvapiReady;
}

void UnloadNVAPI() {
    if (g_NvAPI_Unload) g_NvAPI_Unload();
    if (g_hNvApiDll) {
        FreeLibrary(g_hNvApiDll);
        g_hNvApiDll = nullptr;
    }
}

void ApplyVibrancePercent(int percent) {
    if (!g_nvapiReady) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    for (void* hDisp : g_displayHandles) {
        NV_DISPLAY_DVC_INFO info = {};
        info.version = (unsigned int)(sizeof(NV_DISPLAY_DVC_INFO) | 0x10000);
        int minLvl = 0, maxLvl = 63;
        if (g_NvAPI_GetDVCInfo && g_NvAPI_GetDVCInfo(hDisp, 0, &info) == 0) {
            minLvl = info.minLevel;
            maxLvl = info.maxLevel;
        }
        int level = minLvl + (int)((float)(maxLvl - minLvl) * (percent / 100.0f) + 0.5f);
        g_NvAPI_SetDVCLevel(hDisp, 0, level);
    }
}

void ApplySharpnessPercent(int percent) {
    if (!g_nvapiReady || !g_NvAPI_OverlaySharpness) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    for (void* hDisp : g_displayHandles) {
        g_NvAPI_OverlaySharpness(hDisp, percent);
    }
}

// --- Detecção Passiva de Processo & Fullscreen ---
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
        procName = (pos != std::wstring::npos) ? s.substr(pos + 1) : s;
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

void UpdateStatusUI(const std::wstring& statusText, bool isGame) {
    HWND hStatus = GetDlgItem(g_hMainWindow, IDC_STATUS_LABEL);
    if (hStatus) {
        std::wstring prefix = isGame ? L"🎮 JOGO EM FOCO: " : L"🟢 STATUS: ";
        SetWindowTextW(hStatus, (prefix + statusText).c_str());
    }

    std::wstring tip = L"VibranceApp - " + statusText;
    wcsncpy(g_nid.szTip, tip.c_str(), sizeof(g_nid.szTip) / sizeof(WCHAR) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void CheckActiveGameFocus() {
    HWND fgHwnd = nullptr;
    std::wstring fgProc = GetForegroundProcessName(&fgHwnd);
    if (fgProc.empty()) return;

    GameProfile* matchedProfile = nullptr;
    for (auto& profile : g_settings.profiles) {
        if (!profile.enabled) continue;
        if (profile.exeName == fgProc) {
            if (!profile.applyWindowed && !IsWindowFullscreen(fgHwnd)) {
                continue;
            }
            matchedProfile = &profile;
            break;
        }
    }

    if (matchedProfile) {
        if (!g_isGameActive || g_activeGameName != matchedProfile->name) {
            ApplyVibrancePercent(matchedProfile->vibrance);
            if (matchedProfile->sharpness > 0) {
                ApplySharpnessPercent(matchedProfile->sharpness);
            }
            g_isGameActive = true;
            g_activeGameName = matchedProfile->name;
            UpdateStatusUI(matchedProfile->name + L" (Saturação: " + std::to_wstring(matchedProfile->vibrance) + L"%)", true);
        }
    } else {
        if (g_isGameActive) {
            ApplyVibrancePercent(g_settings.windowsVibrance);
            ApplySharpnessPercent(0);
            g_isGameActive = false;
            g_activeGameName = L"";
            UpdateStatusUI(L"Em Espera (Área de Trabalho: " + std::to_wstring(g_settings.windowsVibrance) + L"%)", false);
        }
    }
}

// --- Atualização da Lista e Inputs ---
void RefreshProfileList() {
    HWND hList = GetDlgItem(g_hMainWindow, IDC_PROFILE_LIST);
    if (!hList) return;
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    for (size_t i = 0; i < g_settings.profiles.size(); i++) {
        std::wstring item = (g_settings.profiles[i].enabled ? L"✓ " : L"✗ ") +
                            g_settings.profiles[i].name + L" [" +
                            g_settings.profiles[i].exeName + L"] (" +
                            std::to_wstring(g_settings.profiles[i].vibrance) + L"%)";
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }

    if (!g_settings.profiles.empty()) {
        if (g_selectedProfileIndex >= (int)g_settings.profiles.size()) {
            g_selectedProfileIndex = (int)g_settings.profiles.size() - 1;
        }
        SendMessageW(hList, LB_SETCURSEL, g_selectedProfileIndex, 0);
    }
}

void LoadSelectedProfileToInputs() {
    if (g_selectedProfileIndex < 0 || g_selectedProfileIndex >= (int)g_settings.profiles.size()) return;
    const GameProfile& p = g_settings.profiles[g_selectedProfileIndex];

    SetWindowTextW(GetDlgItem(g_hMainWindow, IDC_EDIT_GAME_NAME), p.name.c_str());
    SetWindowTextW(GetDlgItem(g_hMainWindow, IDC_EDIT_GAME_EXE), p.exeName.c_str());

    SendMessageW(GetDlgItem(g_hMainWindow, IDC_SLIDER_GAME_VIB), TBM_SETPOS, TRUE, p.vibrance);
    SetWindowTextW(GetDlgItem(g_hMainWindow, IDC_LBL_GAME_VIB_VAL), (std::to_wstring(p.vibrance) + L"%").c_str());

    SendMessageW(GetDlgItem(g_hMainWindow, IDC_SLIDER_GAME_SHARP), TBM_SETPOS, TRUE, p.sharpness);
    SetWindowTextW(GetDlgItem(g_hMainWindow, IDC_LBL_GAME_SHARP_VAL), (std::to_wstring(p.sharpness) + L"%").c_str());

    SendMessageW(GetDlgItem(g_hMainWindow, IDC_CHK_WINDOWED), BM_SETCHECK, p.applyWindowed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g_hMainWindow, IDC_CHK_PROFILE_ENABLED), BM_SETCHECK, p.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SaveInputsToSelectedProfile() {
    if (g_selectedProfileIndex < 0 || g_selectedProfileIndex >= (int)g_settings.profiles.size()) return;
    GameProfile& p = g_settings.profiles[g_selectedProfileIndex];

    WCHAR nameBuf[128] = {0};
    WCHAR exeBuf[128] = {0};
    GetWindowTextW(GetDlgItem(g_hMainWindow, IDC_EDIT_GAME_NAME), nameBuf, 128);
    GetWindowTextW(GetDlgItem(g_hMainWindow, IDC_EDIT_GAME_EXE), exeBuf, 128);

    std::wstring exe(exeBuf);
    for (auto& c : exe) c = towlower(c);

    p.name = nameBuf;
    p.exeName = exe;
    p.vibrance = (int)SendMessageW(GetDlgItem(g_hMainWindow, IDC_SLIDER_GAME_VIB), TBM_GETPOS, 0, 0);
    p.sharpness = (int)SendMessageW(GetDlgItem(g_hMainWindow, IDC_SLIDER_GAME_SHARP), TBM_GETPOS, 0, 0);
    p.applyWindowed = (SendMessageW(GetDlgItem(g_hMainWindow, IDC_CHK_WINDOWED), BM_GETCHECK, 0, 0) == BST_CHECKED);
    p.enabled = (SendMessageW(GetDlgItem(g_hMainWindow, IDC_CHK_PROFILE_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED);

    SaveSettings();
    RefreshProfileList();
}

// Troca de Abas
void SwitchTab(int newTab) {
    g_currentTab = newTab;
    for (HWND h : g_tab0Controls) ShowWindow(h, newTab == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab1Controls) ShowWindow(h, newTab == 1 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab2Controls) ShowWindow(h, newTab == 2 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab3Controls) ShowWindow(h, newTab == 3 ? SW_SHOW : SW_HIDE);

    for (int i = 0; i < 4; i++) {
        InvalidateRect(g_tabButtons[i], NULL, TRUE);
    }
    InvalidateRect(g_hMainWindow, NULL, TRUE);
}

// Dialogo de Seleção de Processos Abertos
void ShowRunningProcessesDialog(HWND hParent) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    std::vector<std::wstring> exes;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring exe(pe.szExeFile);
            for (auto& c : exe) c = towlower(c);
            if (exe.length() > 4 && exe.substr(exe.length() - 4) == L".exe") {
                if (exe != L"svchost.exe" && exe != L"explorer.exe" && exe != L"csrss.exe" &&
                    exe != L"vibranceapp.exe" && exe != L"cs2vibrance.exe") {
                    if (std::find(exes.begin(), exes.end(), exe) == exes.end()) {
                        exes.push_back(exe);
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    std::sort(exes.begin(), exes.end());

    if (exes.empty()) {
        MessageBoxW(hParent, L"Nenhum processo de jogo ou aplicativo detectado no momento.", L"Processos", MB_OK);
        return;
    }

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"Selecionar Jogo em Execução",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                320, 200, 400, 440, hParent, NULL, GetModuleHandle(NULL), NULL);

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hDlg, 20, &darkMode, sizeof(darkMode));

    HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                                 20, 20, 345, 300, hDlg, (HMENU)501, GetModuleHandle(NULL), NULL);

    HWND hBtnSelect = CreateWindowW(L"BUTTON", L"Selecionar Processo",
                                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    20, 335, 165, 38, hDlg, (HMENU)IDOK, GetModuleHandle(NULL), NULL);

    HWND hBtnCancel = CreateWindowW(L"BUTTON", L"Cancelar",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    200, 335, 165, 38, hDlg, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);

    SendMessageW(hList, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);
    SendMessageW(hBtnSelect, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);

    for (const auto& e : exes) {
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)e.c_str());
    }
    SendMessageW(hList, LB_SETCURSEL, 0, 0);

    EnableWindow(hParent, FALSE);
    MSG msg;
    bool done = false;
    while (!done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_COMMAND) {
            int id = LOWORD(msg.wParam);
            if (id == IDOK) {
                int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)exes.size()) {
                    SetWindowTextW(GetDlgItem(hParent, IDC_EDIT_GAME_EXE), exes[sel].c_str());
                    std::wstring baseName = exes[sel].substr(0, exes[sel].length() - 4);
                    baseName[0] = towupper(baseName[0]);
                    SetWindowTextW(GetDlgItem(hParent, IDC_EDIT_GAME_NAME), baseName.c_str());
                }
                done = true;
            } else if (id == IDCANCEL) {
                done = true;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hParent, TRUE);
    DestroyWindow(hDlg);
    SetForegroundWindow(hParent);
}

void BrowseExeFile(HWND hParent) {
    WCHAR fileBuf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hParent;
    ofn.lpstrFilter = L"Executáveis (*.exe)\0*.exe\0Todos os Arquivos (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring path(fileBuf);
        size_t pos = path.find_last_of(L"\\/");
        std::wstring exe = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
        for (auto& c : exe) c = towlower(c);

        SetWindowTextW(GetDlgItem(hParent, IDC_EDIT_GAME_EXE), exe.c_str());
        std::wstring baseName = (exe.length() > 4) ? exe.substr(0, exe.length() - 4) : exe;
        baseName[0] = towupper(baseName[0]);
        SetWindowTextW(GetDlgItem(hParent, IDC_EDIT_GAME_NAME), baseName.c_str());
    }
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Abrir Painel VibranceApp");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_TRAY_STATUS,
                g_isGameActive ? (L"🎮 Jogo Ativo: " + g_activeGameName).c_str() : L"🟢 Modo Standby (Windows)");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESTORE_WIN, L"Restaurar Cores Padrão (50%)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Sair");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// Pintura Customizada dos Botões (OwnerDraw)
void DrawStyledButton(LPDRAWITEMSTRUCT dis) {
    int id = dis->CtlID;
    bool isPressed = (dis->itemState & ODS_SELECTED);

    COLORREF bgColor = RGB(45, 45, 58);
    COLORREF fgColor = RGB(240, 240, 245);
    COLORREF borderColor = RGB(65, 65, 80);

    // Abas de Navegação Superiores
    if (id >= IDC_TAB_BTN_0 && id <= IDC_TAB_BTN_3) {
        int tabIdx = id - IDC_TAB_BTN_0;
        if (g_currentTab == tabIdx) {
            bgColor = RGB(37, 99, 235);    // Azul Ativo (#2563eb)
            fgColor = RGB(255, 255, 255);
            borderColor = RGB(59, 130, 246);
        } else {
            bgColor = isPressed ? RGB(25, 25, 32) : RGB(32, 32, 40);
            fgColor = RGB(160, 160, 175);
            borderColor = RGB(48, 48, 60);
        }
    }
    // Botões Específicos
    else if (id == IDC_BTN_ADD_GAME) {
        bgColor = isPressed ? RGB(16, 140, 78) : RGB(22, 163, 74);  // Verde Esmeralda
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_REMOVE_GAME) {
        bgColor = isPressed ? RGB(180, 40, 40) : RGB(220, 38, 38);  // Vermelho
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_SAVE_PROFILES || id == IDC_BTN_SAVE_SETTINGS || id == IDC_BTN_APPLY_TEST) {
        bgColor = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);  // Azul Primário
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_RESTORE_DEFAULT || id == IDC_BTN_BROWSE_EXE || id == IDC_BTN_DETECT_RUNNING) {
        bgColor = isPressed ? RGB(38, 38, 50) : RGB(50, 50, 65);   // Cinza Slate
        fgColor = RGB(230, 230, 240);
        borderColor = RGB(70, 70, 90);
    }

    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldBrush = SelectObject(dis->hDC, hBrush);
    HGDIOBJ oldPen = SelectObject(dis->hDC, hPen);

    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom, 8, 8);

    SelectObject(dis->hDC, oldBrush);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    WCHAR text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fgColor);
    SelectObject(dis->hDC, g_fontBold);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// Pintura de Fundo e Cards da Janela Principal
void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    // 1. Fundo Global Dark (#121216)
    FillRect(hdc, &rcClient, g_hBrushBg);

    // 2. Banner do Topo (Header)
    RECT rcHeader = {0, 0, rcClient.right, 68};
    HBRUSH hHeaderBrush = CreateSolidBrush(RGB(18, 18, 24));
    FillRect(hdc, &rcHeader, hHeaderBrush);
    DeleteObject(hHeaderBrush);

    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(38, 38, 48));
    HPEN oldPen = (HPEN)SelectObject(hdc, hPenBorder);
    MoveToEx(hdc, 0, 68, NULL);
    LineTo(hdc, rcClient.right, 68);

    // 3. Rodapé / Status Bar
    RECT rcFooter = {0, rcClient.bottom - 38, rcClient.right, rcClient.bottom};
    HBRUSH hFooterBrush = CreateSolidBrush(RGB(16, 16, 20));
    FillRect(hdc, &rcFooter, hFooterBrush);
    DeleteObject(hFooterBrush);

    MoveToEx(hdc, 0, rcClient.bottom - 38, NULL);
    LineTo(hdc, rcClient.right, rcClient.bottom - 38);
    SelectObject(hdc, oldPen);
    DeleteObject(hPenBorder);

    // 4. Desenho de Cards por Aba
    if (g_currentTab == 0) {
        // Aba 0: Jogos & Perfis -> Dois Cards (Esquerda e Direita)
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 34));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(42, 42, 54));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        // Card Lista (Esquerda)
        RoundRect(hdc, 22, 120, 305, 575, 10, 10);
        // Card Edição (Direita)
        RoundRect(hdc, 318, 120, 815, 575, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);
    }
    else if (g_currentTab == 1 || g_currentTab == 2) {
        // Abas 1 e 2: Card Central Grande
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 34));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(42, 42, 54));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        RoundRect(hdc, 22, 120, 815, 575, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        // Se for a Aba de Teste, desenhar barra de gradiente cromático
        if (g_currentTab == 1) {
            RECT rcGrad = {45, 360, 790, 395};
            int width = rcGrad.right - rcGrad.left;
            for (int x = 0; x < width; x++) {
                float t = (float)x / (float)width;
                int r = (int)(255 * (0.5f + 0.5f * sin(t * 6.28f)));
                int g = (int)(255 * (0.5f + 0.5f * sin(t * 6.28f + 2.09f)));
                int b = (int)(255 * (0.5f + 0.5f * sin(t * 6.28f + 4.18f)));
                HPEN hGradPen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
                SelectObject(hdc, hGradPen);
                MoveToEx(hdc, rcGrad.left + x, rcGrad.top, NULL);
                LineTo(hdc, rcGrad.left + x, rcGrad.bottom);
                DeleteObject(hGradPen);
            }
        }
    }
    else if (g_currentTab == 3) {
        // Aba 3: Guia Didático -> 4 Cards Informativos
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 34));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 56));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        // 4 Cards Empilhados
        RoundRect(hdc, 25, 125, 815, 220, 10, 10);
        RoundRect(hdc, 25, 230, 815, 325, 10, 10);
        RoundRect(hdc, 25, 335, 815, 430, 10, 10);
        RoundRect(hdc, 25, 440, 815, 555, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        SetBkMode(hdc, TRANSPARENT);

        // Card 1
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(56, 189, 248)); // Azul Céu
        TextOutW(hdc, 45, 140, L"⚡ O que é Digital Vibrance?", 26);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 210));
        RECT rc1 = {45, 165, 795, 215};
        DrawTextW(hdc, L"É um ajuste direto de hardware da sua GPU NVIDIA que intensifica a saturação de cores antes do sinal ir para o monitor. Em jogos de tiro competitivos, faz as silhuetas de inimigos e miras se destacarem fortemente de fundos escuros, sem nenhuma perda de FPS.", -1, &rc1, DT_WORDBREAK | DT_NOPREFIX);

        // Card 2
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(74, 222, 128)); // Verde
        TextOutW(hdc, 45, 245, L"🛡️ 100% Seguro Contra Anti-Cheat (VAC, Vanguard, EAC, BattlEye)", 61);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 210));
        RECT rc2 = {45, 270, 795, 320};
        DrawTextW(hdc, L"Diferente de filtros ReShade que injetam código no executável do jogo, o VibranceApp opera 100% externamente. Ele não injeta DLLs, não altera arquivos e não lê memória dos jogos. Ele utiliza exclusivamente as rotinas oficiais do driver da NVIDIA (NVAPI).", -1, &rc2, DT_WORDBREAK | DT_NOPREFIX);

        // Card 3
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(192, 132, 252)); // Roxo
        TextOutW(hdc, 45, 350, L"🔍 Nitidez (Image Sharpening) e Resoluções Esticadas (Ex: 4:3)", 61);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 210));
        RECT rc3 = {45, 375, 795, 425};
        DrawTextW(hdc, L"Aumenta a precisão de contornos e texturas distantes. É especialmente útil para jogadores que usam resoluções 4:3 esticadas (como 1280x960 no CS2) onde a interpolação do monitor tende a deixar o jogo embaçado.", -1, &rc3, DT_WORDBREAK | DT_NOPREFIX);

        // Card 4
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(250, 204, 21)); // Amarelo Dourado
        TextOutW(hdc, 45, 455, L"🔄 Alternância Automática Inteligente", 38);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 210));
        RECT rc4 = {45, 480, 795, 545};
        DrawTextW(hdc, L"O aplicativo monitora passivamente qual janela está em foco. Ao entrar no jogo, aplica o perfil configurado instantaneamente. Ao sair do jogo ou pressionar Alt+Tab para navegar no Windows, a saturação volta aos 50% naturais de descanso visual.", -1, &rc4, DT_WORDBREAK | DT_NOPREFIX);
    }

    EndPaint(hwnd, &ps);
}

// Procedimento Principal da Janela
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWindow = hwnd;

            // Ativar Dark Mode Moderno no Windows 10/11
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

            // Fontes Modernas
            g_fontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontTab = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontBold = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontRegular = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // Pincéis Dark
            g_hBrushBg = CreateSolidBrush(RGB(18, 18, 22));
            g_hBrushCard = CreateSolidBrush(RGB(26, 26, 34));
            g_hBrushInput = CreateSolidBrush(RGB(34, 34, 44));
            g_hBrushHover = CreateSolidBrush(RGB(45, 45, 58));

            // --- Topo: Título & Subtítulo ---
            HWND hTitle = CreateWindowW(L"STATIC", L"VIBRANCE APP", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 12, 220, 26, hwnd, NULL, NULL, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

            HWND hSub = CreateWindowW(L"STATIC", L"Controle Inteligente de Saturação e Nitidez para Jogos", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 40, 420, 20, hwnd, NULL, NULL, NULL);
            SendMessageW(hSub, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            // Informações de Hardware no Topo
            std::wstring gpuText = L"Hardware: " + g_gpuName + L" (" + std::to_wstring(g_displayHandles.size()) + L" Monitor)";
            HWND hGpu = CreateWindowW(L"STATIC", gpuText.c_str(), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, 480, 22, 335, 20, hwnd, (HMENU)IDC_GPU_LABEL, NULL, NULL);
            SendMessageW(hGpu, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            // Rótulo de Status no Rodapé
            HWND hStatus = CreateWindowW(L"STATIC", L"🟢 STATUS: Em Espera (Monitorando jogos cadastrados)", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 608, 790, 22, hwnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageW(hStatus, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            // --- Barra de Navegação Superior (4 Abas Customizadas) ---
            g_tabButtons[0] = CreateWindowW(L"BUTTON", L"🎮 Jogos e Perfis", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 25, 78, 185, 34, hwnd, (HMENU)IDC_TAB_BTN_0, NULL, NULL);
            g_tabButtons[1] = CreateWindowW(L"BUTTON", L"🎨 Teste Ao Vivo", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 218, 78, 175, 34, hwnd, (HMENU)IDC_TAB_BTN_1, NULL, NULL);
            g_tabButtons[2] = CreateWindowW(L"BUTTON", L"⚙️ Configurações", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 401, 78, 185, 34, hwnd, (HMENU)IDC_TAB_BTN_2, NULL, NULL);
            g_tabButtons[3] = CreateWindowW(L"BUTTON", L"ℹ️ Guia Didático", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 594, 78, 220, 34, hwnd, (HMENU)IDC_TAB_BTN_3, NULL, NULL);

            // ==========================================
            // ABA 0: Jogos & Perfis
            // ==========================================
            HWND hLblList = CreateWindowW(L"STATIC", L"Jogos Cadastrados:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 40, 135, 250, 20, hwnd, NULL, NULL, NULL);
            HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY, 38, 160, 252, 335, hwnd, (HMENU)IDC_PROFILE_LIST, NULL, NULL);

            HWND hBtnAdd = CreateWindowW(L"BUTTON", L"+ Novo Jogo", WS_CHILD | BS_OWNERDRAW, 38, 510, 120, 38, hwnd, (HMENU)IDC_BTN_ADD_GAME, NULL, NULL);
            HWND hBtnRemove = CreateWindowW(L"BUTTON", L"- Remover", WS_CHILD | BS_OWNERDRAW, 170, 510, 120, 38, hwnd, (HMENU)IDC_BTN_REMOVE_GAME, NULL, NULL);

            HWND hLblName = CreateWindowW(L"STATIC", L"Nome do Jogo:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 340, 135, 200, 18, hwnd, NULL, NULL, NULL);
            HWND hEditName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 340, 155, 455, 26, hwnd, (HMENU)IDC_EDIT_GAME_NAME, NULL, NULL);

            HWND hLblExe = CreateWindowW(L"STATIC", L"Executável (.exe):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 340, 190, 200, 18, hwnd, NULL, NULL, NULL);
            HWND hEditExe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 340, 210, 215, 26, hwnd, (HMENU)IDC_EDIT_GAME_EXE, NULL, NULL);

            HWND hBtnDetect = CreateWindowW(L"BUTTON", L"🔍 Detectar Abertos", WS_CHILD | BS_OWNERDRAW, 565, 208, 120, 30, hwnd, (HMENU)IDC_BTN_DETECT_RUNNING, NULL, NULL);
            HWND hBtnBrowse = CreateWindowW(L"BUTTON", L"📁 Procurar .exe", WS_CHILD | BS_OWNERDRAW, 693, 208, 102, 30, hwnd, (HMENU)IDC_BTN_BROWSE_EXE, NULL, NULL);

            HWND hLblVib = CreateWindowW(L"STATIC", L"Saturação no Jogo (Digital Vibrance):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 340, 250, 320, 18, hwnd, NULL, NULL, NULL);
            HWND hLblVibVal = CreateWindowW(L"STATIC", L"100%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 715, 250, 80, 18, hwnd, (HMENU)IDC_LBL_GAME_VIB_VAL, NULL, NULL);
            HWND hSliderVib = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 340, 270, 455, 30, hwnd, (HMENU)IDC_SLIDER_GAME_VIB, NULL, NULL);
            SendMessageW(hSliderVib, TBM_SETRANGE, TRUE, MAKELPARAM(50, 100));

            HWND hLblSharp = CreateWindowW(L"STATIC", L"Nitidez no Jogo (Image Sharpening):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 340, 310, 320, 18, hwnd, NULL, NULL, NULL);
            HWND hLblSharpVal = CreateWindowW(L"STATIC", L"0%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 715, 310, 80, 18, hwnd, (HMENU)IDC_LBL_GAME_SHARP_VAL, NULL, NULL);
            HWND hSliderSharp = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 340, 330, 455, 30, hwnd, (HMENU)IDC_SLIDER_GAME_SHARP, NULL, NULL);
            SendMessageW(hSliderSharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));

            HWND hChkWin = CreateWindowW(L"BUTTON", L"Ativar mesmo em Modo Janela / Janela Sem Bordas", WS_CHILD | BS_AUTOCHECKBOX, 340, 380, 440, 22, hwnd, (HMENU)IDC_CHK_WINDOWED, NULL, NULL);
            HWND hChkEn = CreateWindowW(L"BUTTON", L"Perfil Habilitado para este Jogo", WS_CHILD | BS_AUTOCHECKBOX, 340, 410, 440, 22, hwnd, (HMENU)IDC_CHK_PROFILE_ENABLED, NULL, NULL);

            HWND hBtnSaveProfiles = CreateWindowW(L"BUTTON", L"💾 Salvar Alterações do Perfil", WS_CHILD | BS_OWNERDRAW, 340, 475, 455, 42, hwnd, (HMENU)IDC_BTN_SAVE_PROFILES, NULL, NULL);

            g_tab0Controls = {hLblList, hList, hBtnAdd, hBtnRemove, hLblName, hEditName, hLblExe, hEditExe,
                              hBtnDetect, hBtnBrowse, hLblVib, hLblVibVal, hSliderVib, hLblSharp, hLblSharpVal,
                              hSliderSharp, hChkWin, hChkEn, hBtnSaveProfiles};

            // ==========================================
            // ABA 1: Teste Rápido / Ao Vivo
            // ==========================================
            HWND hLblT1 = CreateWindowW(L"STATIC", L"Teste em Tempo Real no seu Monitor", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 140, 500, 22, hwnd, NULL, NULL, NULL);
            HWND hLblT2 = CreateWindowW(L"STATIC", L"Arraste os controles abaixo para visualizar a saturação e nitidez mudando diretamente na sua tela agora mesmo.", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 165, 740, 20, hwnd, NULL, NULL, NULL);

            HWND hLblTVib = CreateWindowW(L"STATIC", L"Nível de Saturação para Teste (0% a 100%):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 205, 400, 18, hwnd, NULL, NULL, NULL);
            HWND hLblTVibVal = CreateWindowW(L"STATIC", L"100%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 700, 205, 90, 18, hwnd, (HMENU)IDC_LBL_TEST_VIB_VAL, NULL, NULL);
            HWND hSliderTVib = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 45, 228, 745, 32, hwnd, (HMENU)IDC_SLIDER_TEST_VIB, NULL, NULL);
            SendMessageW(hSliderTVib, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderTVib, TBM_SETPOS, TRUE, 100);

            HWND hLblTSharp = CreateWindowW(L"STATIC", L"Nível de Nitidez para Teste (0% a 100%):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 275, 400, 18, hwnd, NULL, NULL, NULL);
            HWND hLblTSharpVal = CreateWindowW(L"STATIC", L"0%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 700, 275, 90, 18, hwnd, (HMENU)IDC_LBL_TEST_SHARP_VAL, NULL, NULL);
            HWND hSliderTSharp = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 45, 298, 745, 32, hwnd, (HMENU)IDC_SLIDER_TEST_SHARP, NULL, NULL);
            SendMessageW(hSliderTSharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderTSharp, TBM_SETPOS, TRUE, 0);

            HWND hLblGrad = CreateWindowW(L"STATIC", L"Espectro de Cores de Referência:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 340, 400, 18, hwnd, NULL, NULL, NULL);

            HWND hBtnApplyTest = CreateWindowW(L"BUTTON", L"👁️ Aplicar Teste no Monitor", WS_CHILD | BS_OWNERDRAW, 45, 420, 360, 42, hwnd, (HMENU)IDC_BTN_APPLY_TEST, NULL, NULL);
            HWND hBtnRestore = CreateWindowW(L"BUTTON", L"🔄 Restaurar Padrão Windows (50%)", WS_CHILD | BS_OWNERDRAW, 430, 420, 360, 42, hwnd, (HMENU)IDC_BTN_RESTORE_DEFAULT, NULL, NULL);

            g_tab1Controls = {hLblT1, hLblT2, hLblTVib, hLblTVibVal, hSliderTVib, hLblTSharp, hLblTSharpVal,
                              hSliderTSharp, hLblGrad, hBtnApplyTest, hBtnRestore};

            // ==========================================
            // ABA 2: Configurações Gerais
            // ==========================================
            HWND hLblG1 = CreateWindowW(L"STATIC", L"Saturação Padrão da Área de Trabalho (Windows Desktop):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 140, 500, 18, hwnd, NULL, NULL, NULL);
            HWND hLblWinVibVal = CreateWindowW(L"STATIC", L"50%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 700, 140, 90, 18, hwnd, (HMENU)IDC_LBL_WIN_VIB_VAL, NULL, NULL);
            HWND hSliderWinVib = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 45, 165, 745, 32, hwnd, (HMENU)IDC_SLIDER_WIN_VIB, NULL, NULL);
            SendMessageW(hSliderWinVib, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderWinVib, TBM_SETPOS, TRUE, g_settings.windowsVibrance);

            HWND hChkAutostart = CreateWindowW(L"BUTTON", L"Iniciar junto com o Windows (Minimizado silenciosamente na bandeja)", WS_CHILD | BS_AUTOCHECKBOX, 45, 220, 680, 22, hwnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
            SendMessageW(hChkAutostart, BM_SETCHECK, g_settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

            HWND hChkMinTray = CreateWindowW(L"BUTTON", L"Minimizar para a bandeja do sistema ao fechar a janela (Botão X)", WS_CHILD | BS_AUTOCHECKBOX, 45, 255, 680, 22, hwnd, (HMENU)IDC_CHK_MINIMIZE_TRAY, NULL, NULL);
            SendMessageW(hChkMinTray, BM_SETCHECK, g_settings.minimizeToTrayOnClose ? BST_CHECKED : BST_UNCHECKED, 0);

            HWND hLblInt = CreateWindowW(L"STATIC", L"Taxa de Verificação de Foco (Tempo de Resposta):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 45, 300, 400, 18, hwnd, NULL, NULL, NULL);
            HWND hComboInt = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 45, 325, 320, 120, hwnd, (HMENU)IDC_COMBO_INTERVAL, NULL, NULL);
            SendMessageW(hComboInt, CB_ADDSTRING, 0, (LPARAM)L"250 ms (Ultragarrápido - Recomendado)");
            SendMessageW(hComboInt, CB_ADDSTRING, 0, (LPARAM)L"500 ms (Equilibrado - Padrão 0% CPU)");
            SendMessageW(hComboInt, CB_ADDSTRING, 0, (LPARAM)L"1000 ms (Econômico)");
            SendMessageW(hComboInt, CB_SETCURSEL, g_settings.pollingIntervalMs == 250 ? 0 : (g_settings.pollingIntervalMs == 1000 ? 2 : 1), 0);

            HWND hBtnSaveGen = CreateWindowW(L"BUTTON", L"💾 Salvar Preferências Gerais", WS_CHILD | BS_OWNERDRAW, 45, 420, 360, 42, hwnd, (HMENU)IDC_BTN_SAVE_SETTINGS, NULL, NULL);

            g_tab2Controls = {hLblG1, hLblWinVibVal, hSliderWinVib, hChkAutostart, hChkMinTray, hLblInt, hComboInt, hBtnSaveGen};

            // Fontes
            SendMessageW(hLblT1, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            for (HWND h : g_tab0Controls) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);
            for (HWND h : g_tab1Controls) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);
            for (HWND h : g_tab2Controls) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);

            // Iniciar com a Aba 0
            SwitchTab(0);
            RefreshProfileList();
            LoadSelectedProfileToInputs();

            // Configurar Ícone na Bandeja
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_APP_TRAY;
            g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcsncpy(g_nid.szTip, L"VibranceApp - Standby", sizeof(g_nid.szTip) / sizeof(WCHAR) - 1);
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            // Timer de Verificação Passiva de Foco (0% CPU)
            SetTimer(hwnd, IDT_POLL_TIMER, g_settings.pollingIntervalMs, NULL);
            break;
        }

        case WM_TIMER:
            if (wParam == IDT_POLL_TIMER) {
                CheckActiveGameFocus();
            }
            break;

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                DrawStyledButton(dis);
                return TRUE;
            }
            break;
        }

        case WM_HSCROLL: {
            HWND hScroll = (HWND)lParam;
            int pos = (int)SendMessageW(hScroll, TBM_GETPOS, 0, 0);
            std::wstring valStr = std::to_wstring(pos) + L"%";

            if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_GAME_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_GAME_VIB_VAL), valStr.c_str());
            } else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_GAME_SHARP)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_GAME_SHARP_VAL), valStr.c_str());
            } else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_TEST_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_TEST_VIB_VAL), valStr.c_str());
                ApplyVibrancePercent(pos);
            } else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_TEST_SHARP)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_TEST_SHARP_VAL), valStr.c_str());
                ApplySharpnessPercent(pos);
            } else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_WIN_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_WIN_VIB_VAL), valStr.c_str());
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            // Abas de Navegação
            if (wmId >= IDC_TAB_BTN_0 && wmId <= IDC_TAB_BTN_3) {
                SwitchTab(wmId - IDC_TAB_BTN_0);
            }
            else if (wmId == IDC_PROFILE_LIST && wmEvent == LBN_SELCHANGE) {
                g_selectedProfileIndex = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
                LoadSelectedProfileToInputs();
            }
            else if (wmId == IDC_BTN_ADD_GAME) {
                GameProfile newGame = {L"Novo Jogo", L"jogo.exe", 100, 0, true, true};
                g_settings.profiles.push_back(newGame);
                g_selectedProfileIndex = (int)g_settings.profiles.size() - 1;
                SaveSettings();
                RefreshProfileList();
                LoadSelectedProfileToInputs();
            }
            else if (wmId == IDC_BTN_REMOVE_GAME) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    g_settings.profiles.erase(g_settings.profiles.begin() + g_selectedProfileIndex);
                    if (g_selectedProfileIndex >= (int)g_settings.profiles.size()) {
                        g_selectedProfileIndex = (int)g_settings.profiles.size() - 1;
                    }
                    SaveSettings();
                    RefreshProfileList();
                    LoadSelectedProfileToInputs();
                }
            }
            else if (wmId == IDC_BTN_DETECT_RUNNING) {
                ShowRunningProcessesDialog(hwnd);
            }
            else if (wmId == IDC_BTN_BROWSE_EXE) {
                BrowseExeFile(hwnd);
            }
            else if (wmId == IDC_BTN_SAVE_PROFILES) {
                SaveInputsToSelectedProfile();
                MessageBoxW(hwnd, L"Perfil de jogo atualizado e salvo com sucesso!", L"VibranceApp", MB_OK | MB_ICONINFORMATION);
            }
            else if (wmId == IDC_BTN_APPLY_TEST) {
                int vib = (int)SendMessageW(GetDlgItem(hwnd, IDC_SLIDER_TEST_VIB), TBM_GETPOS, 0, 0);
                int shp = (int)SendMessageW(GetDlgItem(hwnd, IDC_SLIDER_TEST_SHARP), TBM_GETPOS, 0, 0);
                ApplyVibrancePercent(vib);
                ApplySharpnessPercent(shp);
            }
            else if (wmId == IDC_BTN_RESTORE_DEFAULT) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                ApplySharpnessPercent(0);
                SendMessageW(GetDlgItem(hwnd, IDC_SLIDER_TEST_VIB), TBM_SETPOS, TRUE, g_settings.windowsVibrance);
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_TEST_VIB_VAL), (std::to_wstring(g_settings.windowsVibrance) + L"%").c_str());
            }
            else if (wmId == IDC_BTN_SAVE_SETTINGS) {
                g_settings.windowsVibrance = (int)SendMessageW(GetDlgItem(hwnd, IDC_SLIDER_WIN_VIB), TBM_GETPOS, 0, 0);
                g_settings.startWithWindows = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_settings.minimizeToTrayOnClose = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_MINIMIZE_TRAY), BM_GETCHECK, 0, 0) == BST_CHECKED);

                int selInt = (int)SendMessageW(GetDlgItem(hwnd, IDC_COMBO_INTERVAL), CB_GETCURSEL, 0, 0);
                g_settings.pollingIntervalMs = (selInt == 0) ? 250 : ((selInt == 2) ? 1000 : 500);

                SetTimer(hwnd, IDT_POLL_TIMER, g_settings.pollingIntervalMs, NULL);
                SaveSettings();
                MessageBoxW(hwnd, L"Preferências gerais salvas com sucesso!", L"VibranceApp", MB_OK | MB_ICONINFORMATION);
            }
            else if (wmId == IDM_TRAY_OPEN) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            else if (wmId == IDM_TRAY_RESTORE_WIN) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                ApplySharpnessPercent(0);
            }
            else if (wmId == IDM_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_APP_TRAY:
            if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayMenu(hwnd, pt);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            break;

        // Customização de Cores das Caixas de Texto (Dark Theme Elegante)
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetBkColor(hdcEdit, RGB(34, 34, 44));
            SetTextColor(hdcEdit, RGB(255, 255, 255));
            return (LRESULT)g_hBrushInput;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = (HDC)wParam;
            SetBkColor(hdcList, RGB(26, 26, 34));
            SetTextColor(hdcList, RGB(240, 240, 245));
            return (LRESULT)g_hBrushCard;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            SetBkMode(hdcStatic, TRANSPARENT);

            // Destaques coloridos
            if (hCtrl == GetDlgItem(hwnd, IDC_LBL_GAME_VIB_VAL) || hCtrl == GetDlgItem(hwnd, IDC_LBL_TEST_VIB_VAL)) {
                SetTextColor(hdcStatic, RGB(56, 189, 248)); // Azul Céu
            } else if (hCtrl == GetDlgItem(hwnd, IDC_LBL_GAME_SHARP_VAL) || hCtrl == GetDlgItem(hwnd, IDC_LBL_TEST_SHARP_VAL)) {
                SetTextColor(hdcStatic, RGB(192, 132, 252)); // Roxo Suave
            } else if (hCtrl == GetDlgItem(hwnd, IDC_STATUS_LABEL)) {
                SetTextColor(hdcStatic, g_isGameActive ? RGB(56, 189, 248) : RGB(74, 222, 128));
            } else if (hCtrl == GetDlgItem(hwnd, IDC_GPU_LABEL)) {
                SetTextColor(hdcStatic, RGB(156, 163, 175));
            } else {
                SetTextColor(hdcStatic, RGB(235, 235, 242));
            }

            // Retornar o pincel de card para que não haja bordas/fundos pretos discordantes
            return (LRESULT)g_hBrushCard;
        }

        case WM_CLOSE:
            if (g_settings.minimizeToTrayOnClose) {
                ShowWindow(hwnd, SW_HIDE);
                g_nid.uFlags |= NIF_INFO;
                wcsncpy(g_nid.szInfo, L"VibranceApp continua ativo em segundo plano monitorando seus jogos.", sizeof(g_nid.szInfo)/sizeof(WCHAR)-1);
                wcsncpy(g_nid.szInfoTitle, L"VibranceApp Ativo", sizeof(g_nid.szInfoTitle)/sizeof(WCHAR)-1);
                g_nid.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &g_nid);
                g_nid.uFlags &= ~NIF_INFO;
                return 0;
            }
            break;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, IDT_POLL_TIMER);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            ApplyVibrancePercent(g_settings.windowsVibrance);
            ApplySharpnessPercent(0);
            UnloadNVAPI();

            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontTab) DeleteObject(g_fontTab);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontRegular) DeleteObject(g_fontRegular);
            if (g_fontSmall) DeleteObject(g_fontSmall);

            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushCard) DeleteObject(g_hBrushCard);
            if (g_hBrushInput) DeleteObject(g_hBrushInput);
            if (g_hBrushHover) DeleteObject(g_hBrushHover);

            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Ponto de Entrada WinMain ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hMutex = CreateMutexW(NULL, TRUE, L"VibranceApp~Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"VibranceAppMainWindowClass", NULL);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    LoadSettings();

    if (!InitNVAPI()) {
        MessageBoxW(NULL,
                    L"Aviso: Não foi possível conectar diretamente à API NVIDIA (NVAPI).\n"
                    L"O aplicativo funcionará em modo demonstração e teste de interface.",
                    L"Aviso de Hardware", MB_OK | MB_ICONWARNING);
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"VibranceAppMainWindowClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"VibranceApp - Controle de Saturação e Nitidez",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 850, 680, NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        if (g_hMutex) CloseHandle(g_hMutex);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

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

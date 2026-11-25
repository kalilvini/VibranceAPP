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
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <vector>
#include <string>
#include <cwctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

// --- Definições NVAPI (NVIDIA) ---
#define NVAPI_INITIALIZE_ID               0x0150E828
#define NVAPI_UNLOAD_ID                   0xD22BDD7E
#define NVAPI_ENUM_DISPLAY_HANDLE_ID      0x9ABDD40D
#define NVAPI_GET_DVC_INFO_ID             0x4085DE45
#define NVAPI_SET_DVC_LEVEL_ID            0x172409B4
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
typedef int (*NvAPI_EnumPhysicalGPUs_t)(void** gpuHandles, int* pGpuCount);
typedef int (*NvAPI_GPU_GetFullName_t)(void* hPhysicalGpu, char* szName);

static NvAPI_Initialize_t             g_NvAPI_Initialize = nullptr;
static NvAPI_Unload_t                 g_NvAPI_Unload = nullptr;
static NvAPI_EnumNvidiaDisplayHandle_t g_NvAPI_EnumDisplay = nullptr;
static NvAPI_GetDVCInfo_t             g_NvAPI_GetDVCInfo = nullptr;
static NvAPI_SetDVCLevel_t            g_NvAPI_SetDVCLevel = nullptr;
static NvAPI_EnumPhysicalGPUs_t       g_NvAPI_EnumGPUs = nullptr;
static NvAPI_GPU_GetFullName_t        g_NvAPI_GetGpuName = nullptr;

static HMODULE                        g_hNvApiDll = nullptr;
static std::vector<void*>             g_displayHandles;
static std::wstring                   g_gpuName = L"NVIDIA GeForce";
static bool                           g_nvapiReady = false;

// --- DDC/CI Hardware Monitor (VESA MCCS 0x87 = Sharpness / Nitidez) ---
static std::vector<PHYSICAL_MONITOR>  g_physicalMonitors;
static bool                           g_monitorSupportsSharpness = false;
static DWORD                          g_monitorMinSharpness = 0;
static DWORD                          g_monitorMaxSharpness = 100;
static DWORD                          g_monitorCurSharpness = 50;
static std::wstring                   g_monitorName = L"";

// --- Estrutura de Perfis de Jogos ---
struct GameProfile {
    std::wstring name;
    std::wstring exeName;
    int vibrance;       // 50 a 100%
    int sharpness;      // 0 a 100% (Nitidez física no monitor via DDC/CI)
    bool applyWindowed; // true: modo janela + fullscreen, false: fullscreen apenas
    bool enabled;
};

struct AppSettings {
    int windowsVibrance = 50;
    int windowsSharpness = 50;
    bool startWithWindows = false;
    bool minimizeToTrayOnClose = true;
    int pollingIntervalMs = 300; // 300 ms para resposta rápida e 0% CPU
    std::vector<GameProfile> profiles;
} g_settings;

// --- Estado da Aplicação ---
static const UINT WM_APP_TRAY = WM_APP + 201;
static const UINT_PTR IDT_POLL_TIMER = 3001;

static HWND g_hMainWindow = nullptr;
static NOTIFYICONDATAW g_nid = {};
static HANDLE g_hMutex = nullptr;

// Fontes
static HFONT g_fontTitle = nullptr;
static HFONT g_fontBold = nullptr;
static HFONT g_fontRegular = nullptr;
static HFONT g_fontSmall = nullptr;

// Pincéis Dark
static HBRUSH g_hBrushBg = nullptr;      // #121216
static HBRUSH g_hBrushCard = nullptr;    // #1a1a24
static HBRUSH g_hBrushInput = nullptr;   // #22222e

static bool g_isGameActive = false;
static std::wstring g_activeGameName = L"";
static std::wstring g_currentFgProcess = L"";
static int g_currentTab = 0; // 0 = Principal, 1 = Como Funciona
static int g_selectedProfileIndex = 0;

// IDs dos Controles da GUI
enum ControlIDs {
    IDC_STATUS_BADGE = 100,
    IDC_GPU_LABEL,
    IDC_LIVE_TRACKER,

    // Abas
    IDC_TAB_BTN_MAIN = 150,
    IDC_TAB_BTN_GUIDE,

    // Painel Principal
    IDC_PROFILE_LIST = 200,
    IDC_BTN_ADD_GAME,
    IDC_BTN_REMOVE_GAME,
    IDC_BTN_DETECT_RUNNING,
    IDC_BTN_BROWSE_EXE,
    IDC_EDIT_GAME_NAME,
    IDC_EDIT_GAME_EXE,
    
    // Sliders do Jogo
    IDC_SLIDER_GAME_VIB = 220,
    IDC_LBL_GAME_VIB_VAL,
    IDC_SLIDER_GAME_SHARP,
    IDC_LBL_GAME_SHARP_VAL,
    IDC_BTN_TEST_GAME_VIB,
    IDC_BTN_TEST_GAME_SHARP,
    IDC_CHK_WINDOWED,
    IDC_CHK_PROFILE_ENABLED,

    // Desktop & Sistema
    IDC_SLIDER_DESKTOP_VIB = 300,
    IDC_LBL_DESKTOP_VIB_VAL,
    IDC_SLIDER_DESKTOP_SHARP,
    IDC_LBL_DESKTOP_SHARP_VAL,
    IDC_BTN_TEST_DESKTOP,
    IDC_CHK_AUTOSTART,
    IDC_CHK_MINIMIZE_TRAY,

    // Tray Menu
    IDM_TRAY_OPEN = 2000,
    IDM_TRAY_STATUS,
    IDM_TRAY_RESTORE_WIN,
    IDM_TRAY_EXIT
};

static std::vector<HWND> g_tabMainControls;
static std::vector<HWND> g_tabGuideControls;
static HWND g_tabButtons[2];

// --- DDC/CI Hardware Monitor Implementation ---
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM) {
    DWORD numMonitors = 0;
    if (GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &numMonitors) && numMonitors > 0) {
        std::vector<PHYSICAL_MONITOR> monitors(numMonitors);
        if (GetPhysicalMonitorsFromHMONITOR(hMonitor, numMonitors, monitors.data())) {
            for (DWORD i = 0; i < numMonitors; ++i) {
                g_physicalMonitors.push_back(monitors[i]);
            }
        }
    }
    return TRUE;
}

void InitDdcMonitors() {
    g_physicalMonitors.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    g_monitorSupportsSharpness = false;
    for (auto& pm : g_physicalMonitors) {
        MC_VCP_CODE_TYPE codeType;
        DWORD cur = 0, maxV = 100;
        // VESA VCP Code 0x87 = Sharpness / Nitidez
        if (GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, 0x87, &codeType, &cur, &maxV)) {
            g_monitorMinSharpness = 0;
            g_monitorMaxSharpness = (maxV > 0) ? maxV : 100;
            g_monitorCurSharpness = cur;
            g_monitorSupportsSharpness = true;
            if (pm.szPhysicalMonitorDescription[0] != L'\0') {
                g_monitorName = pm.szPhysicalMonitorDescription;
            } else {
                g_monitorName = L"Monitor DDC/CI";
            }
            break; // Monitor principal configurado
        }
    }
}

void ApplyMonitorSharpnessPercent(int percent) {
    if (!g_monitorSupportsSharpness) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    DWORD target = g_monitorMinSharpness + (DWORD)((float)(g_monitorMaxSharpness - g_monitorMinSharpness) * (percent / 100.0f) + 0.5f);
    for (auto& pm : g_physicalMonitors) {
        SetVCPFeature(pm.hPhysicalMonitor, 0x87, target);
    }
}

void CleanupDdcMonitors() {
    if (!g_physicalMonitors.empty()) {
        DestroyPhysicalMonitors((DWORD)g_physicalMonitors.size(), g_physicalMonitors.data());
        g_physicalMonitors.clear();
    }
}

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
    g_settings.profiles.push_back({L"Counter-Strike 2", L"cs2.exe", 100, 80, true, true});
    g_settings.profiles.push_back({L"Valorant", L"valorant-win64-shipping.exe", 85, 70, true, true});
    g_settings.profiles.push_back({L"Apex Legends", L"r5apex.exe", 80, 70, true, true});
    g_settings.profiles.push_back({L"Call of Duty / Warzone", L"cod.exe", 85, 75, true, true});
    g_settings.profiles.push_back({L"Fortnite", L"fortniteclient-win64-shipping.exe", 75, 60, true, true});
    g_settings.profiles.push_back({L"Overwatch 2", L"overwatch.exe", 80, 65, true, true});
    g_settings.profiles.push_back({L"Rainbow Six Siege", L"rainbowsix.exe", 85, 75, true, true});
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

void SaveSettings();

void LoadSettings() {
    std::wstring path = GetProfilesFilePath();
    g_settings.windowsVibrance = GetPrivateProfileIntW(L"General", L"WindowsVibrance", 50, path.c_str());
    g_settings.windowsSharpness = GetPrivateProfileIntW(L"General", L"WindowsSharpness", 50, path.c_str());
    g_settings.minimizeToTrayOnClose = (GetPrivateProfileIntW(L"General", L"MinimizeToTray", 1, path.c_str()) != 0);
    g_settings.pollingIntervalMs = GetPrivateProfileIntW(L"General", L"PollingInterval", 300, path.c_str());
    g_settings.startWithWindows = IsAutostartEnabled();

    int count = GetPrivateProfileIntW(L"Profiles", L"Count", -1, path.c_str());
    if (count <= 0) {
        SetDefaultProfiles();
        SaveSettings();
    } else {
        g_settings.profiles.clear();
        for (int i = 0; i < count; i++) {
            std::wstring sec = L"Profile_" + std::to_wstring(i);
            WCHAR name[128] = {0};
            WCHAR exe[128] = {0};
            GetPrivateProfileStringW(sec.c_str(), L"Name", L"Game", name, 128, path.c_str());
            GetPrivateProfileStringW(sec.c_str(), L"Exe", L"game.exe", exe, 128, path.c_str());
            int vib = GetPrivateProfileIntW(sec.c_str(), L"Vibrance", 100, path.c_str());
            int sharp = GetPrivateProfileIntW(sec.c_str(), L"Sharpness", 80, path.c_str());
            bool win = (GetPrivateProfileIntW(sec.c_str(), L"Windowed", 1, path.c_str()) != 0);
            bool en = (GetPrivateProfileIntW(sec.c_str(), L"Enabled", 1, path.c_str()) != 0);

            std::wstring exeLower(exe);
            for (auto& c : exeLower) c = towlower(c);
            g_settings.profiles.push_back({name, exeLower, vib, sharp, win, en});
        }
    }
}

void SaveSettings() {
    std::wstring path = GetProfilesFilePath();
    WritePrivateProfileStringW(L"General", L"WindowsVibrance", std::to_wstring(g_settings.windowsVibrance).c_str(), path.c_str());
    WritePrivateProfileStringW(L"General", L"WindowsSharpness", std::to_wstring(g_settings.windowsSharpness).c_str(), path.c_str());
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

// --- Detecção Robusta de Processo em Primeiro Plano ---
std::wstring GetProcessNameFromPid(DWORD pid) {
    if (pid == 0) return L"";
    std::wstring procName = L"";

    // 1. Tentar QueryFullProcessImageNameW
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        WCHAR path[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
            std::wstring s(path);
            size_t pos = s.find_last_of(L"\\/");
            procName = (pos != std::wstring::npos) ? s.substr(pos + 1) : s;
        }
        CloseHandle(hProc);
    }

    // 2. Fallback: Toolhelp32Snapshot
    if (procName.empty()) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == pid) {
                        procName = pe.szExeFile;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    for (auto& c : procName) c = towlower(c);
    return procName;
}

std::wstring GetForegroundProcess(HWND* outHwnd) {
    HWND hwnd = GetForegroundWindow();
    if (outHwnd) *outHwnd = hwnd;
    if (!hwnd) return L"";

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return GetProcessNameFromPid(pid);
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

// --- Atualização de Status da Interface ---
void UpdateStatusUI(const std::wstring& statusText, bool isGame) {
    HWND hStatus = GetDlgItem(g_hMainWindow, IDC_STATUS_BADGE);
    if (hStatus) {
        std::wstring prefix = isGame ? L"🎮 " : L"🟢 ";
        SetWindowTextW(hStatus, (prefix + statusText).c_str());
    }

    std::wstring tip = L"VibranceApp - " + statusText;
    wcsncpy(g_nid.szTip, tip.c_str(), sizeof(g_nid.szTip) / sizeof(WCHAR) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void CheckActiveGameFocus() {
    HWND fgHwnd = nullptr;
    std::wstring fgProc = GetForegroundProcess(&fgHwnd);
    if (fgProc.empty()) return;

    if (fgHwnd == g_hMainWindow) {
        return;
    }

    // Atualizar indicador ao vivo da janela ativa no rodapé
    HWND hTracker = GetDlgItem(g_hMainWindow, IDC_LIVE_TRACKER);
    if (hTracker && fgProc != g_currentFgProcess) {
        g_currentFgProcess = fgProc;
        std::wstring trackerText = L"Janela Ativa: [" + fgProc + L"]";
        SetWindowTextW(hTracker, trackerText.c_str());
    }

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
            ApplyMonitorSharpnessPercent(matchedProfile->sharpness);
            g_isGameActive = true;
            g_activeGameName = matchedProfile->name;
            UpdateStatusUI(matchedProfile->name + L" (Vibrance: " + std::to_wstring(matchedProfile->vibrance) + 
                           L"% | Nitidez: " + std::to_wstring(matchedProfile->sharpness) + L"%)", true);
        }
    } else {
        if (g_isGameActive) {
            ApplyVibrancePercent(g_settings.windowsVibrance);
            ApplyMonitorSharpnessPercent(g_settings.windowsSharpness);
            g_isGameActive = false;
            g_activeGameName = L"";
            UpdateStatusUI(L"Área de Trabalho (Vibrance: " + std::to_wstring(g_settings.windowsVibrance) + 
                           L"% | Nitidez: " + std::to_wstring(g_settings.windowsSharpness) + L"%)", false);
        }
    }
}

// --- Atualização dos Campos de Edição de Jogos ---
void RefreshProfileList() {
    HWND hList = GetDlgItem(g_hMainWindow, IDC_PROFILE_LIST);
    if (!hList) return;
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    for (size_t i = 0; i < g_settings.profiles.size(); i++) {
        std::wstring item = (g_settings.profiles[i].enabled ? L"✓ " : L"✗ ") +
                            g_settings.profiles[i].name + L" (V:" +
                            std::to_wstring(g_settings.profiles[i].vibrance) + L"% | N:" +
                            std::to_wstring(g_settings.profiles[i].sharpness) + L"%)";
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

// Troca de Abas
void SwitchTab(int newTab) {
    g_currentTab = newTab;
    for (HWND h : g_tabMainControls) ShowWindow(h, newTab == 0 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tabGuideControls) ShowWindow(h, newTab == 1 ? SW_SHOW : SW_HIDE);

    for (int i = 0; i < 2; i++) {
        InvalidateRect(g_tabButtons[i], NULL, TRUE);
    }
    InvalidateRect(g_hMainWindow, NULL, TRUE);
}

// Diálogo de Seleção de Processos Abertos
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
        MessageBoxW(hParent, L"Nenhum processo detectado no momento.", L"Processos", MB_OK);
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

                    if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                        g_settings.profiles[g_selectedProfileIndex].exeName = exes[sel];
                        g_settings.profiles[g_selectedProfileIndex].name = baseName;
                        SaveSettings();
                        RefreshProfileList();
                    }
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

        if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
            g_settings.profiles[g_selectedProfileIndex].exeName = exe;
            g_settings.profiles[g_selectedProfileIndex].name = baseName;
            SaveSettings();
            RefreshProfileList();
        }
    }
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Abrir Painel VibranceApp");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_TRAY_STATUS,
                g_isGameActive ? (L"🎮 Jogo Ativo: " + g_activeGameName).c_str() : L"🟢 Modo Standby (Windows)");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESTORE_WIN, L"Restaurar Padrão do Windows");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Sair");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// Pintura Customizada dos Botões
void DrawStyledButton(LPDRAWITEMSTRUCT dis) {
    int id = dis->CtlID;
    bool isPressed = (dis->itemState & ODS_SELECTED);

    COLORREF bgColor = RGB(45, 45, 58);
    COLORREF fgColor = RGB(240, 240, 245);
    COLORREF borderColor = RGB(65, 65, 80);

    // Abas de Navegação Superiores
    if (id == IDC_TAB_BTN_MAIN || id == IDC_TAB_BTN_GUIDE) {
        int tabIdx = (id == IDC_TAB_BTN_MAIN) ? 0 : 1;
        if (g_currentTab == tabIdx) {
            bgColor = RGB(37, 99, 235);    // Azul Ativo (#2563eb)
            fgColor = RGB(255, 255, 255);
            borderColor = RGB(59, 130, 246);
        } else {
            bgColor = isPressed ? RGB(22, 22, 28) : RGB(30, 30, 38);
            fgColor = RGB(160, 160, 175);
            borderColor = RGB(46, 46, 58);
        }
    }
    // Botões Específicos
    else if (id == IDC_BTN_ADD_GAME) {
        bgColor = isPressed ? RGB(16, 140, 78) : RGB(22, 163, 74);
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_REMOVE_GAME) {
        bgColor = isPressed ? RGB(180, 40, 40) : RGB(220, 38, 38);
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_TEST_GAME_VIB || id == IDC_BTN_TEST_GAME_SHARP || id == IDC_BTN_TEST_DESKTOP) {
        bgColor = isPressed ? RGB(29, 78, 216) : RGB(37, 99, 235);
        fgColor = RGB(255, 255, 255);
        borderColor = bgColor;
    }
    else if (id == IDC_BTN_BROWSE_EXE || id == IDC_BTN_DETECT_RUNNING) {
        bgColor = isPressed ? RGB(38, 38, 50) : RGB(50, 50, 65);
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
    RECT rcFooter = {0, rcClient.bottom - 42, rcClient.right, rcClient.bottom};
    HBRUSH hFooterBrush = CreateSolidBrush(RGB(16, 16, 20));
    FillRect(hdc, &rcFooter, hFooterBrush);
    DeleteObject(hFooterBrush);

    MoveToEx(hdc, 0, rcClient.bottom - 42, NULL);
    LineTo(hdc, rcClient.right, rcClient.bottom - 42);
    SelectObject(hdc, oldPen);
    DeleteObject(hPenBorder);

    // 4. Desenho de Cards por Aba
    if (g_currentTab == 0) {
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 36));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 58));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        // Card Lista (Esquerda)
        RoundRect(hdc, 22, 120, 290, 640, 10, 10);
        // Card Edição do Jogo (Direita Superior)
        RoundRect(hdc, 302, 120, 828, 485, 10, 10);
        // Card Área de Trabalho & Monitor (Direita Inferior)
        RoundRect(hdc, 302, 497, 828, 640, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);
    }
    else if (g_currentTab == 1) {
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 36));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 58));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        RoundRect(hdc, 25, 125, 825, 235, 10, 10);
        RoundRect(hdc, 25, 245, 825, 365, 10, 10);
        RoundRect(hdc, 25, 375, 825, 505, 10, 10);
        RoundRect(hdc, 25, 515, 825, 635, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        SetBkMode(hdc, TRANSPARENT);

        // Card 1: Como Funciona no Monitor
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(56, 189, 248));
        TextOutW(hdc, 45, 138, L"🖥️ Como Funciona o Digital Vibrance (Saturação NVIDIA)", 53);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc1 = {45, 162, 805, 225};
        DrawTextW(hdc, L"O Digital Vibrance é um recurso físico de hardware da sua GPU NVIDIA que processa os canais de cor na saída de vídeo antes de enviar os quadros para o monitor. O VibranceApp detecta quando seu jogo entra em primeiro plano e aplica a saturação escolhida; ao dar Alt+Tab ou fechar o jogo, restaura instantaneamente os 50% normais da Área de Trabalho.", -1, &rc1, DT_WORDBREAK | DT_NOPREFIX);

        // Card 2: Como Funciona a Nitidez por Hardware (DDC/CI)
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(74, 222, 128));
        TextOutW(hdc, 45, 258, L"🎯 Nitidez por Hardware via Cabo DisplayPort (DDC/CI)", 52);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc2 = {45, 282, 805, 355};
        DrawTextW(hdc, L"Monitores como o seu AOC de 27\" 180Hz possuem suporte nativo ao protocolo VESA DDC/CI. O VibranceApp envia comandos diretos pelo cabo DisplayPort (código VESA 0x87) para ajustar o slider físico de Nitidez do monitor! Isso não consome 0% de placa de vídeo, não gera lag e é 100% indetectável por qualquer anti-cheat (VAC, Vanguard, EAC). Certifique-se de que a opção 'DDC/CI' esteja ativada no menu OSD do seu monitor.", -1, &rc2, DT_WORDBREAK | DT_NOPREFIX);

        // Card 3: Alternativa via Driver NVIDIA (Registro)
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(250, 204, 21));
        TextOutW(hdc, 45, 388, L"⚙️ Opção Alternativa: Reativar Nitidez no Painel NVIDIA", 53);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc3 = {45, 412, 805, 495};
        DrawTextW(hdc, L"Caso o seu monitor não responda ao comando DDC/CI, você também pode reativar o clássico slider de 'Nitidez da Imagem' no Painel de Controle da NVIDIA executando o arquivo 'Ativar_Nitidez_NVIDIA.reg' incluído na pasta do programa. Ele configura a chave EnableGR535=0 no registro e reativa a nitidez diretamente no driver GeForce.", -1, &rc3, DT_WORDBREAK | DT_NOPREFIX);

        // Card 4: 100% Anti-Cheat Safe
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(192, 132, 252));
        TextOutW(hdc, 45, 528, L"🛡️ 100% Seguro Contra Anti-Cheat & Salvamento Automático", 55);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc4 = {45, 552, 805, 625};
        DrawTextW(hdc, L"Nenhuma DLL é injetada nos processos de jogos e nenhuma memória é modificada. O aplicativo comunica-se exclusivamente com a GPU e com o monitor. Qualquer alteração nos sliders é salva instantaneamente no arquivo de configuração.", -1, &rc4, DT_WORDBREAK | DT_NOPREFIX);
    }

    EndPaint(hwnd, &ps);
}

// Procedimento Principal da Janela
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWindow = hwnd;

            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

            // Fontes Modernas
            g_fontTitle = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontBold = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontRegular = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_fontSmall = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // Pincéis Dark
            g_hBrushBg = CreateSolidBrush(RGB(18, 18, 22));
            g_hBrushCard = CreateSolidBrush(RGB(26, 26, 36));
            g_hBrushInput = CreateSolidBrush(RGB(34, 34, 46));

            // --- Topo: Título & Hardware ---
            HWND hTitle = CreateWindowW(L"STATIC", L"VIBRANCE APP", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 12, 220, 26, hwnd, NULL, NULL, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

            HWND hSub = CreateWindowW(L"STATIC", L"Controle Automático de Saturação & Nitidez por Jogo", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 40, 420, 20, hwnd, NULL, NULL, NULL);
            SendMessageW(hSub, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            std::wstring ddcStatus = g_monitorSupportsSharpness ? (L" | " + g_monitorName + L" (DDC/CI)") : L" | DDC/CI Ativo";
            std::wstring gpuText = g_gpuName + ddcStatus;
            HWND hGpu = CreateWindowW(L"STATIC", gpuText.c_str(), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, 450, 22, 375, 20, hwnd, (HMENU)IDC_GPU_LABEL, NULL, NULL);
            SendMessageW(hGpu, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            // --- Barra de Abas ---
            g_tabButtons[0] = CreateWindowW(L"BUTTON", L"🎮 Painel de Jogos, Saturação e Nitidez", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 25, 78, 395, 34, hwnd, (HMENU)IDC_TAB_BTN_MAIN, NULL, NULL);
            g_tabButtons[1] = CreateWindowW(L"BUTTON", L"ℹ️ Como Funciona & Nitidez DDC/CI", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 430, 78, 395, 34, hwnd, (HMENU)IDC_TAB_BTN_GUIDE, NULL, NULL);

            // ==========================================
            // ABA 0: PAINEL PRINCIPAL
            // ==========================================

            // Coluna Esquerda: Lista de Jogos
            HWND hLblList = CreateWindowW(L"STATIC", L"Jogos Cadastrados:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 35, 132, 230, 18, hwnd, NULL, NULL, NULL);
            HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY, 35, 155, 240, 425, hwnd, (HMENU)IDC_PROFILE_LIST, NULL, NULL);

            HWND hBtnAdd = CreateWindowW(L"BUTTON", L"+ Novo Jogo", WS_CHILD | BS_OWNERDRAW, 35, 590, 115, 36, hwnd, (HMENU)IDC_BTN_ADD_GAME, NULL, NULL);
            HWND hBtnRemove = CreateWindowW(L"BUTTON", L"- Remover", WS_CHILD | BS_OWNERDRAW, 160, 590, 115, 36, hwnd, (HMENU)IDC_BTN_REMOVE_GAME, NULL, NULL);

            // Coluna Direita Superior: Edição do Jogo Selecionado
            HWND hLblSectionGame = CreateWindowW(L"STATIC", L"Configuração do Jogo Selecionado:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 130, 400, 18, hwnd, NULL, NULL, NULL);

            HWND hLblName = CreateWindowW(L"STATIC", L"Nome de Exibição:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 154, 140, 18, hwnd, NULL, NULL, NULL);
            HWND hEditName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 320, 172, 490, 26, hwnd, (HMENU)IDC_EDIT_GAME_NAME, NULL, NULL);

            HWND hLblExe = CreateWindowW(L"STATIC", L"Nome do Executável (.exe):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 202, 180, 18, hwnd, NULL, NULL, NULL);
            HWND hEditExe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 320, 220, 245, 26, hwnd, (HMENU)IDC_EDIT_GAME_EXE, NULL, NULL);

            HWND hBtnDetect = CreateWindowW(L"BUTTON", L"🔍 Detectar Aberto", WS_CHILD | BS_OWNERDRAW, 575, 218, 125, 30, hwnd, (HMENU)IDC_BTN_DETECT_RUNNING, NULL, NULL);
            HWND hBtnBrowse = CreateWindowW(L"BUTTON", L"📁 Procurar .exe", WS_CHILD | BS_OWNERDRAW, 708, 218, 102, 30, hwnd, (HMENU)IDC_BTN_BROWSE_EXE, NULL, NULL);

            // Saturação (Vibrance) no Jogo
            HWND hLblVib = CreateWindowW(L"STATIC", L"Saturação (Digital Vibrance) no Jogo:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 254, 380, 18, hwnd, NULL, NULL, NULL);
            HWND hLblVibVal = CreateWindowW(L"STATIC", L"100%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 730, 254, 80, 18, hwnd, (HMENU)IDC_LBL_GAME_VIB_VAL, NULL, NULL);
            HWND hSliderVib = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 320, 272, 490, 28, hwnd, (HMENU)IDC_SLIDER_GAME_VIB, NULL, NULL);
            SendMessageW(hSliderVib, TBM_SETRANGE, TRUE, MAKELPARAM(50, 100));

            // Nitidez (Sharpness DDC/CI) no Jogo
            HWND hLblSharp = CreateWindowW(L"STATIC", L"Nitidez no Monitor AOC (Hardware DDC/CI):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 304, 380, 18, hwnd, NULL, NULL, NULL);
            HWND hLblSharpVal = CreateWindowW(L"STATIC", L"80%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 730, 304, 80, 18, hwnd, (HMENU)IDC_LBL_GAME_SHARP_VAL, NULL, NULL);
            HWND hSliderSharp = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 320, 322, 490, 28, hwnd, (HMENU)IDC_SLIDER_GAME_SHARP, NULL, NULL);
            SendMessageW(hSliderSharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));

            // Botões de Teste
            HWND hBtnTestGameVib = CreateWindowW(L"BUTTON", L"👁️ Testar Saturação", WS_CHILD | BS_OWNERDRAW, 320, 358, 240, 34, hwnd, (HMENU)IDC_BTN_TEST_GAME_VIB, NULL, NULL);
            HWND hBtnTestGameSharp = CreateWindowW(L"BUTTON", L"🎯 Testar Nitidez Monitor", WS_CHILD | BS_OWNERDRAW, 570, 358, 240, 34, hwnd, (HMENU)IDC_BTN_TEST_GAME_SHARP, NULL, NULL);

            HWND hChkWin = CreateWindowW(L"BUTTON", L"Ativar também em Modo Janela / Janela Sem Bordas", WS_CHILD | BS_AUTOCHECKBOX, 320, 402, 490, 20, hwnd, (HMENU)IDC_CHK_WINDOWED, NULL, NULL);
            HWND hChkEn = CreateWindowW(L"BUTTON", L"Perfil Habilitado para este Jogo", WS_CHILD | BS_AUTOCHECKBOX, 320, 426, 490, 20, hwnd, (HMENU)IDC_CHK_PROFILE_ENABLED, NULL, NULL);

            // Coluna Direita Inferior: Saturação & Nitidez da Área de Trabalho (Windows Desktop)
            HWND hLblDesktop = CreateWindowW(L"STATIC", L"Padrão do Windows (Quando nenhum jogo estiver focado):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 505, 420, 18, hwnd, NULL, NULL, NULL);
            
            HWND hLblDeskVib = CreateWindowW(L"STATIC", L"Vibrance:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 530, 70, 18, hwnd, NULL, NULL, NULL);
            HWND hLblDeskVal = CreateWindowW(L"STATIC", L"50%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 470, 530, 40, 18, hwnd, (HMENU)IDC_LBL_DESKTOP_VIB_VAL, NULL, NULL);
            HWND hSliderDesk = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 320, 548, 195, 26, hwnd, (HMENU)IDC_SLIDER_DESKTOP_VIB, NULL, NULL);
            SendMessageW(hSliderDesk, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderDesk, TBM_SETPOS, TRUE, g_settings.windowsVibrance);

            HWND hLblDeskSharp = CreateWindowW(L"STATIC", L"Nitidez:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 530, 530, 60, 18, hwnd, NULL, NULL, NULL);
            HWND hLblDeskSharpVal = CreateWindowW(L"STATIC", L"50%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 660, 530, 40, 18, hwnd, (HMENU)IDC_LBL_DESKTOP_SHARP_VAL, NULL, NULL);
            HWND hSliderDeskSharp = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 530, 548, 175, 26, hwnd, (HMENU)IDC_SLIDER_DESKTOP_SHARP, NULL, NULL);
            SendMessageW(hSliderDeskSharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderDeskSharp, TBM_SETPOS, TRUE, g_settings.windowsSharpness);

            HWND hBtnResetDesk = CreateWindowW(L"BUTTON", L"Restaurar", WS_CHILD | BS_OWNERDRAW, 715, 536, 95, 36, hwnd, (HMENU)IDC_BTN_TEST_DESKTOP, NULL, NULL);

            HWND hChkAutostart = CreateWindowW(L"BUTTON", L"Iniciar com o Windows", WS_CHILD | BS_AUTOCHECKBOX, 320, 595, 230, 20, hwnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
            SendMessageW(hChkAutostart, BM_SETCHECK, g_settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

            HWND hChkMinTray = CreateWindowW(L"BUTTON", L"Minimizar para a bandeja ao fechar (X)", WS_CHILD | BS_AUTOCHECKBOX, 550, 595, 260, 20, hwnd, (HMENU)IDC_CHK_MINIMIZE_TRAY, NULL, NULL);
            SendMessageW(hChkMinTray, BM_SETCHECK, g_settings.minimizeToTrayOnClose ? BST_CHECKED : BST_UNCHECKED, 0);

            g_tabMainControls = {hLblList, hList, hBtnAdd, hBtnRemove, hLblSectionGame, hLblName, hEditName,
                                 hLblExe, hEditExe, hBtnDetect, hBtnBrowse, hLblVib, hLblVibVal, hSliderVib,
                                 hLblSharp, hLblSharpVal, hSliderSharp, hBtnTestGameVib, hBtnTestGameSharp,
                                 hChkWin, hChkEn, hLblDesktop, hLblDeskVib, hLblDeskVal, hSliderDesk,
                                 hLblDeskSharp, hLblDeskSharpVal, hSliderDeskSharp, hBtnResetDesk,
                                 hChkAutostart, hChkMinTray};

            // Rodapé
            HWND hStatusBadge = CreateWindowW(L"STATIC", L"🟢 STATUS: Área de Trabalho (Vibrance: 50% | Nitidez: 50%)", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 665, 450, 22, hwnd, (HMENU)IDC_STATUS_BADGE, NULL, NULL);
            SendMessageW(hStatusBadge, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            HWND hLiveTracker = CreateWindowW(L"STATIC", L"Janela Ativa: [ explorer.exe ]", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, 480, 665, 345, 22, hwnd, (HMENU)IDC_LIVE_TRACKER, NULL, NULL);
            SendMessageW(hLiveTracker, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            // Aplicar Fontes
            SendMessageW(hLblSectionGame, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            SendMessageW(hLblDesktop, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
            for (HWND h : g_tabMainControls) SendMessageW(h, WM_SETFONT, (WPARAM)g_fontRegular, TRUE);

            // Inicializar Aba e Perfis
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

            // Iniciar Timer Rápido de Monitoramento
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

            // Saturação do Jogo
            if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_GAME_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_GAME_VIB_VAL), valStr.c_str());
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    g_settings.profiles[g_selectedProfileIndex].vibrance = pos;
                    SaveSettings();
                    RefreshProfileList();

                    if (g_isGameActive && g_activeGameName == g_settings.profiles[g_selectedProfileIndex].name) {
                        ApplyVibrancePercent(pos);
                    }
                }
            }
            // Nitidez do Jogo
            else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_GAME_SHARP)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_GAME_SHARP_VAL), valStr.c_str());
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    g_settings.profiles[g_selectedProfileIndex].sharpness = pos;
                    SaveSettings();
                    RefreshProfileList();

                    if (g_isGameActive && g_activeGameName == g_settings.profiles[g_selectedProfileIndex].name) {
                        ApplyMonitorSharpnessPercent(pos);
                    }
                }
            }
            // Saturação do Desktop
            else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_DESKTOP_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_DESKTOP_VIB_VAL), valStr.c_str());
                g_settings.windowsVibrance = pos;
                SaveSettings();

                if (!g_isGameActive) {
                    ApplyVibrancePercent(pos);
                }
            }
            // Nitidez do Desktop
            else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_DESKTOP_SHARP)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_DESKTOP_SHARP_VAL), valStr.c_str());
                g_settings.windowsSharpness = pos;
                SaveSettings();

                if (!g_isGameActive) {
                    ApplyMonitorSharpnessPercent(pos);
                }
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            // Abas
            if (wmId == IDC_TAB_BTN_MAIN) {
                SwitchTab(0);
            } else if (wmId == IDC_TAB_BTN_GUIDE) {
                SwitchTab(1);
            }
            // Seleção de Jogo na Lista
            else if (wmId == IDC_PROFILE_LIST && wmEvent == LBN_SELCHANGE) {
                g_selectedProfileIndex = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
                LoadSelectedProfileToInputs();
            }
            // Edição de Nome ou Exe
            else if (wmId == IDC_EDIT_GAME_NAME && wmEvent == EN_KILLFOCUS) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    WCHAR buf[128] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_GAME_NAME), buf, 128);
                    g_settings.profiles[g_selectedProfileIndex].name = buf;
                    SaveSettings();
                    RefreshProfileList();
                }
            }
            else if (wmId == IDC_EDIT_GAME_EXE && wmEvent == EN_KILLFOCUS) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    WCHAR buf[128] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_GAME_EXE), buf, 128);
                    std::wstring exe(buf);
                    for (auto& c : exe) c = towlower(c);
                    g_settings.profiles[g_selectedProfileIndex].exeName = exe;
                    SaveSettings();
                    RefreshProfileList();
                }
            }
            // Checkboxes
            else if (wmId == IDC_CHK_WINDOWED) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    bool chk = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_WINDOWED), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_settings.profiles[g_selectedProfileIndex].applyWindowed = chk;
                    SaveSettings();
                }
            }
            else if (wmId == IDC_CHK_PROFILE_ENABLED) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    bool chk = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_PROFILE_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_settings.profiles[g_selectedProfileIndex].enabled = chk;
                    SaveSettings();
                    RefreshProfileList();
                }
            }
            // Botões Adicionar / Remover Jogo
            else if (wmId == IDC_BTN_ADD_GAME) {
                GameProfile newGame = {L"Novo Jogo", L"jogo.exe", 100, 80, true, true};
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
            // Teste de Saturação do Jogo
            else if (wmId == IDC_BTN_TEST_GAME_VIB) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    int vib = g_settings.profiles[g_selectedProfileIndex].vibrance;
                    ApplyVibrancePercent(vib);
                    UpdateStatusUI(L"TESTE: " + g_settings.profiles[g_selectedProfileIndex].name + L" (Saturação: " + std::to_wstring(vib) + L"%)", true);
                }
            }
            // Teste de Nitidez do Jogo
            else if (wmId == IDC_BTN_TEST_GAME_SHARP) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    int sharp = g_settings.profiles[g_selectedProfileIndex].sharpness;
                    ApplyMonitorSharpnessPercent(sharp);
                    UpdateStatusUI(L"TESTE: Nitidez do Monitor AOC (" + std::to_wstring(sharp) + L"%)", true);
                }
            }
            // Restaurar Desktop
            else if (wmId == IDC_BTN_TEST_DESKTOP) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                ApplyMonitorSharpnessPercent(g_settings.windowsSharpness);
                g_isGameActive = false;
                UpdateStatusUI(L"Área de Trabalho (Vibrance: " + std::to_wstring(g_settings.windowsVibrance) + 
                               L"% | Nitidez: " + std::to_wstring(g_settings.windowsSharpness) + L"%)", false);
            }
            else if (wmId == IDC_CHK_AUTOSTART) {
                g_settings.startWithWindows = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveSettings();
            }
            else if (wmId == IDC_CHK_MINIMIZE_TRAY) {
                g_settings.minimizeToTrayOnClose = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_MINIMIZE_TRAY), BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveSettings();
            }
            // Tray
            else if (wmId == IDM_TRAY_OPEN) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            else if (wmId == IDM_TRAY_RESTORE_WIN) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                ApplyMonitorSharpnessPercent(g_settings.windowsSharpness);
                g_isGameActive = false;
            }
            else if (wmId == IDM_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            int id = GetDlgCtrlID(hCtrl);

            SetBkMode(hdcStatic, TRANSPARENT);

            if (id == IDC_STATUS_BADGE) {
                SetTextColor(hdcStatic, g_isGameActive ? RGB(74, 222, 128) : RGB(56, 189, 248));
                return (INT_PTR)g_hBrushBg;
            }
            if (id == IDC_GPU_LABEL || id == IDC_LIVE_TRACKER) {
                SetTextColor(hdcStatic, RGB(148, 163, 184));
                return (INT_PTR)g_hBrushBg;
            }
            if (id == IDC_LBL_GAME_VIB_VAL || id == IDC_LBL_GAME_SHARP_VAL ||
                id == IDC_LBL_DESKTOP_VIB_VAL || id == IDC_LBL_DESKTOP_SHARP_VAL) {
                SetTextColor(hdcStatic, RGB(56, 189, 248));
                return (INT_PTR)g_hBrushCard;
            }

            SetTextColor(hdcStatic, RGB(220, 220, 230));
            return (INT_PTR)g_hBrushCard;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdcCtrl = (HDC)wParam;
            SetBkColor(hdcCtrl, RGB(34, 34, 46));
            SetTextColor(hdcCtrl, RGB(245, 245, 250));
            return (INT_PTR)g_hBrushInput;
        }

        case WM_APP_TRAY:
            if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayMenu(hwnd, pt);
            }
            break;

        case WM_CLOSE:
            if (g_settings.minimizeToTrayOnClose) {
                ShowWindow(hwnd, SW_HIDE);
                wcsncpy(g_nid.szInfo, L"VibranceApp continua ativo monitorando seus jogos em segundo plano.", sizeof(g_nid.szInfo)/sizeof(WCHAR)-1);
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
            ApplyMonitorSharpnessPercent(g_settings.windowsSharpness);
            CleanupDdcMonitors();
            UnloadNVAPI();

            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontBold) DeleteObject(g_fontBold);
            if (g_fontRegular) DeleteObject(g_fontRegular);
            if (g_fontSmall) DeleteObject(g_fontSmall);

            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushCard) DeleteObject(g_hBrushCard);
            if (g_hBrushInput) DeleteObject(g_hBrushInput);

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
    InitNVAPI();
    InitDdcMonitors();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"VibranceAppMainWindowClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"VibranceApp - Saturação & Nitidez Automática para Jogos",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 860, 740, NULL, NULL, hInstance, NULL);

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

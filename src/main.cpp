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
static std::wstring                   g_gpuName = L"NVIDIA GeForce (Detectando...)";
static bool                           g_nvapiReady = false;

// --- Estrutura de Perfis de Jogos ---
struct GameProfile {
    std::wstring name;
    std::wstring exeName;
    int vibrance;       // 50 a 100%
    bool applyWindowed; // true: modo janela + fullscreen, false: fullscreen apenas
    bool enabled;
};

struct AppSettings {
    int windowsVibrance = 50;
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
static HBRUSH g_hBrushInput = nullptr;   // #252532

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
    IDC_SLIDER_GAME_VIB,
    IDC_LBL_GAME_VIB_VAL,
    IDC_BTN_TEST_GAME_VIB,
    IDC_CHK_WINDOWED,
    IDC_CHK_PROFILE_ENABLED,

    // Desktop Vibrance & Sistema
    IDC_SLIDER_DESKTOP_VIB = 300,
    IDC_LBL_DESKTOP_VIB_VAL,
    IDC_BTN_TEST_DESKTOP_VIB,
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
    g_settings.profiles.push_back({L"Counter-Strike 2", L"cs2.exe", 100, true, true});
    g_settings.profiles.push_back({L"Valorant", L"valorant-win64-shipping.exe", 85, true, true});
    g_settings.profiles.push_back({L"Apex Legends", L"r5apex.exe", 80, true, true});
    g_settings.profiles.push_back({L"Call of Duty / Warzone", L"cod.exe", 85, true, true});
    g_settings.profiles.push_back({L"Fortnite", L"fortniteclient-win64-shipping.exe", 75, true, true});
    g_settings.profiles.push_back({L"Overwatch 2", L"overwatch.exe", 80, true, true});
    g_settings.profiles.push_back({L"Rainbow Six Siege", L"rainbowsix.exe", 85, true, true});
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
            bool win = (GetPrivateProfileIntW(sec.c_str(), L"Windowed", 1, path.c_str()) != 0);
            bool en = (GetPrivateProfileIntW(sec.c_str(), L"Enabled", 1, path.c_str()) != 0);

            std::wstring exeLower(exe);
            for (auto& c : exeLower) c = towlower(c);
            g_settings.profiles.push_back({name, exeLower, vib, win, en});
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

    // 2. Se falhar (ex: jogos protegidos por anti-cheat / privilégios), usar Toolhelp32Snapshot
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

    // Se a janela em foco for a própria janela do VibranceApp, manter estado
    if (fgHwnd == g_hMainWindow) {
        return;
    }

    // Atualizar indicador ao vivo da janela ativa no rodapé
    HWND hTracker = GetDlgItem(g_hMainWindow, IDC_LIVE_TRACKER);
    if (hTracker && fgProc != g_currentFgProcess) {
        g_currentFgProcess = fgProc;
        std::wstring trackerText = L"Janela Ativa Detectada: [" + fgProc + L"]";
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
            g_isGameActive = true;
            g_activeGameName = matchedProfile->name;
            UpdateStatusUI(matchedProfile->name + L" (Saturação: " + std::to_wstring(matchedProfile->vibrance) + L"%)", true);
        }
    } else {
        if (g_isGameActive) {
            ApplyVibrancePercent(g_settings.windowsVibrance);
            g_isGameActive = false;
            g_activeGameName = L"";
            UpdateStatusUI(L"Área de Trabalho (Saturação: " + std::to_wstring(g_settings.windowsVibrance) + L"%)", false);
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
                            g_settings.profiles[i].name + L" (" +
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

                    // Salvar no perfil selecionado
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
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESTORE_WIN, L"Restaurar Cores Padrão (50%)");
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
    else if (id == IDC_BTN_TEST_GAME_VIB || id == IDC_BTN_TEST_DESKTOP_VIB) {
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
        // Aba 0: Principal -> Card Esquerda (Lista), Card Direita Superior (Config Jogo), Card Direita Inferior (Desktop)
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 36));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 58));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        // Card Lista (Esquerda)
        RoundRect(hdc, 22, 120, 290, 580, 10, 10);
        // Card Edição do Jogo (Direita Superior)
        RoundRect(hdc, 302, 120, 818, 440, 10, 10);
        // Card Área de Trabalho & Sistema (Direita Inferior)
        RoundRect(hdc, 302, 452, 818, 580, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);
    }
    else if (g_currentTab == 1) {
        // Aba 1: Guia Didático -> 4 Cards Informativos
        HBRUSH hCardBrush = CreateSolidBrush(RGB(26, 26, 36));
        HPEN hCardPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 58));
        SelectObject(hdc, hCardBrush);
        SelectObject(hdc, hCardPen);

        RoundRect(hdc, 25, 125, 815, 225, 10, 10);
        RoundRect(hdc, 25, 235, 815, 335, 10, 10);
        RoundRect(hdc, 25, 345, 815, 455, 10, 10);
        RoundRect(hdc, 25, 465, 815, 575, 10, 10);

        DeleteObject(hCardBrush);
        DeleteObject(hCardPen);

        SetBkMode(hdc, TRANSPARENT);

        // Card 1: Como Funciona no Monitor
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(56, 189, 248));
        TextOutW(hdc, 45, 140, L"🖥️ Por que a saturação afeta o monitor por inteiro?", 52);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc1 = {45, 165, 795, 220};
        DrawTextW(hdc, L"O Digital Vibrance é um recurso físico de hardware da sua GPU NVIDIA que processa o sinal de vídeo que vai para o monitor. Não é possível saturar apenas uma janela isolada sem afetar a tela. O que o VibranceApp faz de forma inteligente é: quando o jogo está em foco, eleva o monitor para o nível desejado (ex: 100%). Ao dar Alt+Tab ou sair do jogo, volta instantaneamente aos 50% normais do Windows.", -1, &rc1, DT_WORDBREAK | DT_NOPREFIX);

        // Card 2: 100% Anti-Cheat Safe
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(74, 222, 128));
        TextOutW(hdc, 45, 250, L"🛡️ 100% Seguro Contra Anti-Cheat (VAC, Vanguard, EAC, BattlEye)", 61);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc2 = {45, 275, 795, 330};
        DrawTextW(hdc, L"Diferente de filtros ReShade que injetam DLLs dentro dos jogos e causam banimento, o VibranceApp opera 100% externamente. Ele não injeta DLLs, não altera arquivos e não lê memória dos jogos. Ele utiliza exclusivamente as rotinas oficiais do driver da NVIDIA (NVAPI oficial).", -1, &rc2, DT_WORDBREAK | DT_NOPREFIX);

        // Card 3: Sobre Nitidez
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(250, 204, 21));
        TextOutW(hdc, 45, 360, L"⚠️ Nota Importante sobre Nitidez (Image Sharpening) na Série RTX", 64);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc3 = {45, 385, 795, 450};
        DrawTextW(hdc, L"Nas versões modernas do driver NVIDIA para a arquitetura RTX (como a RTX 3060 Ti), a NVIDIA removeu o acesso à API antiga de nitidez direta (0x7A2BC2B2). Por isso, removemos controles de nitidez inoperantes para manter o software 100% funcional. Caso deseje nitidez adicional, recomendamos ativar o Image Scaling no Painel de Controle da NVIDIA ou o filtro Nitidez no GeForce Experience (Alt+F3).", -1, &rc3, DT_WORDBREAK | DT_NOPREFIX);

        // Card 4: Salvamento Automático
        SelectObject(hdc, g_fontBold);
        SetTextColor(hdc, RGB(192, 132, 252));
        TextOutW(hdc, 45, 480, L"⚡ Salvamento Instantâneo & Detecção Contínua", 44);
        SelectObject(hdc, g_fontRegular);
        SetTextColor(hdc, RGB(200, 200, 212));
        RECT rc4 = {45, 505, 795, 570};
        DrawTextW(hdc, L"Qualquer alteração feita nos sliders é salva imediatamente e passa a valer na mesma hora. Você pode verificar na barra inferior do aplicativo exatamente qual janela o Windows está detectando em primeiro plano e qual perfil está ativo em tempo real.", -1, &rc4, DT_WORDBREAK | DT_NOPREFIX);
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

            HWND hSub = CreateWindowW(L"STATIC", L"Controle Automático de Saturação por Jogo (NVIDIA GeForce)", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 40, 450, 20, hwnd, NULL, NULL, NULL);
            SendMessageW(hSub, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            std::wstring gpuText = L"Hardware: " + g_gpuName + L" (" + std::to_wstring(g_displayHandles.size()) + L" Monitor)";
            HWND hGpu = CreateWindowW(L"STATIC", gpuText.c_str(), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, 480, 22, 335, 20, hwnd, (HMENU)IDC_GPU_LABEL, NULL, NULL);
            SendMessageW(hGpu, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

            // --- Barra de Abas Simplificada (2 Abas: Direta e Clara) ---
            g_tabButtons[0] = CreateWindowW(L"BUTTON", L"🎮 Painel de Jogos & Saturação", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 25, 78, 385, 34, hwnd, (HMENU)IDC_TAB_BTN_MAIN, NULL, NULL);
            g_tabButtons[1] = CreateWindowW(L"BUTTON", L"ℹ️ Como Funciona & Segurança", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 420, 78, 395, 34, hwnd, (HMENU)IDC_TAB_BTN_GUIDE, NULL, NULL);

            // ==========================================
            // ABA 0: PAINEL PRINCIPAL (TUDO EM UMA TELA)
            // ==========================================

            // Coluna Esquerda: Lista de Jogos
            HWND hLblList = CreateWindowW(L"STATIC", L"Jogos Cadastrados:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 35, 132, 230, 18, hwnd, NULL, NULL, NULL);
            HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY, 35, 155, 240, 365, hwnd, (HMENU)IDC_PROFILE_LIST, NULL, NULL);

            HWND hBtnAdd = CreateWindowW(L"BUTTON", L"+ Novo Jogo", WS_CHILD | BS_OWNERDRAW, 35, 530, 115, 36, hwnd, (HMENU)IDC_BTN_ADD_GAME, NULL, NULL);
            HWND hBtnRemove = CreateWindowW(L"BUTTON", L"- Remover", WS_CHILD | BS_OWNERDRAW, 160, 530, 115, 36, hwnd, (HMENU)IDC_BTN_REMOVE_GAME, NULL, NULL);

            // Coluna Direita Superior: Edição do Jogo Selecionado
            HWND hLblSectionGame = CreateWindowW(L"STATIC", L"Configuração do Jogo Selecionado:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 132, 400, 18, hwnd, NULL, NULL, NULL);

            HWND hLblName = CreateWindowW(L"STATIC", L"Nome de Exibição:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 158, 140, 18, hwnd, NULL, NULL, NULL);
            HWND hEditName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 320, 178, 480, 26, hwnd, (HMENU)IDC_EDIT_GAME_NAME, NULL, NULL);

            HWND hLblExe = CreateWindowW(L"STATIC", L"Nome do Executável (.exe):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 212, 180, 18, hwnd, NULL, NULL, NULL);
            HWND hEditExe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 320, 232, 235, 26, hwnd, (HMENU)IDC_EDIT_GAME_EXE, NULL, NULL);

            HWND hBtnDetect = CreateWindowW(L"BUTTON", L"🔍 Detectar Aberto", WS_CHILD | BS_OWNERDRAW, 565, 230, 125, 30, hwnd, (HMENU)IDC_BTN_DETECT_RUNNING, NULL, NULL);
            HWND hBtnBrowse = CreateWindowW(L"BUTTON", L"📁 Procurar .exe", WS_CHILD | BS_OWNERDRAW, 698, 230, 102, 30, hwnd, (HMENU)IDC_BTN_BROWSE_EXE, NULL, NULL);

            HWND hLblVib = CreateWindowW(L"STATIC", L"Saturação quando este jogo estiver em foco:", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 272, 380, 18, hwnd, NULL, NULL, NULL);
            HWND hLblVibVal = CreateWindowW(L"STATIC", L"100%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 720, 272, 80, 18, hwnd, (HMENU)IDC_LBL_GAME_VIB_VAL, NULL, NULL);
            HWND hSliderVib = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 320, 294, 480, 30, hwnd, (HMENU)IDC_SLIDER_GAME_VIB, NULL, NULL);
            SendMessageW(hSliderVib, TBM_SETRANGE, TRUE, MAKELPARAM(50, 100));

            HWND hBtnTestGame = CreateWindowW(L"BUTTON", L"👁️ Testar Esta Saturação no Monitor (5 segundos)", WS_CHILD | BS_OWNERDRAW, 320, 335, 480, 34, hwnd, (HMENU)IDC_BTN_TEST_GAME_VIB, NULL, NULL);

            HWND hChkWin = CreateWindowW(L"BUTTON", L"Ativar também em Modo Janela / Janela Sem Bordas", WS_CHILD | BS_AUTOCHECKBOX, 320, 378, 480, 20, hwnd, (HMENU)IDC_CHK_WINDOWED, NULL, NULL);
            HWND hChkEn = CreateWindowW(L"BUTTON", L"Perfil Habilitado para este Jogo", WS_CHILD | BS_AUTOCHECKBOX, 320, 404, 480, 20, hwnd, (HMENU)IDC_CHK_PROFILE_ENABLED, NULL, NULL);

            // Coluna Direita Inferior: Saturação da Área de Trabalho (Windows Desktop)
            HWND hLblDesktop = CreateWindowW(L"STATIC", L"Saturação da Área de Trabalho (Quando nenhum jogo estiver aberto):", WS_CHILD | SS_LEFT | SS_NOPREFIX, 320, 465, 420, 18, hwnd, NULL, NULL, NULL);
            HWND hLblDesktopVal = CreateWindowW(L"STATIC", L"50%", WS_CHILD | SS_RIGHT | SS_NOPREFIX, 740, 465, 60, 18, hwnd, (HMENU)IDC_LBL_DESKTOP_VIB_VAL, NULL, NULL);
            HWND hSliderDesktop = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_NOTICKS, 320, 488, 340, 30, hwnd, (HMENU)IDC_SLIDER_DESKTOP_VIB, NULL, NULL);
            SendMessageW(hSliderDesktop, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSliderDesktop, TBM_SETPOS, TRUE, g_settings.windowsVibrance);

            HWND hBtnTestDesktop = CreateWindowW(L"BUTTON", L"Restaurar Desktop", WS_CHILD | BS_OWNERDRAW, 670, 486, 130, 32, hwnd, (HMENU)IDC_BTN_TEST_DESKTOP_VIB, NULL, NULL);

            HWND hChkAutostart = CreateWindowW(L"BUTTON", L"Iniciar junto com o Windows", WS_CHILD | BS_AUTOCHECKBOX, 320, 528, 230, 20, hwnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
            SendMessageW(hChkAutostart, BM_SETCHECK, g_settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);

            HWND hChkMinTray = CreateWindowW(L"BUTTON", L"Minimizar para a bandeja ao fechar (Botão X)", WS_CHILD | BS_AUTOCHECKBOX, 560, 528, 240, 20, hwnd, (HMENU)IDC_CHK_MINIMIZE_TRAY, NULL, NULL);
            SendMessageW(hChkMinTray, BM_SETCHECK, g_settings.minimizeToTrayOnClose ? BST_CHECKED : BST_UNCHECKED, 0);

            g_tabMainControls = {hLblList, hList, hBtnAdd, hBtnRemove, hLblSectionGame, hLblName, hEditName,
                                 hLblExe, hEditExe, hBtnDetect, hBtnBrowse, hLblVib, hLblVibVal, hSliderVib,
                                 hBtnTestGame, hChkWin, hChkEn, hLblDesktop, hLblDesktopVal, hSliderDesktop,
                                 hBtnTestDesktop, hChkAutostart, hChkMinTray};

            // Rodapé: Status Bar e Rastreador ao Vivo
            HWND hStatusBadge = CreateWindowW(L"STATIC", L"🟢 STATUS: Área de Trabalho (Saturação: 50%)", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 25, 608, 450, 22, hwnd, (HMENU)IDC_STATUS_BADGE, NULL, NULL);
            SendMessageW(hStatusBadge, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            HWND hLiveTracker = CreateWindowW(L"STATIC", L"Janela Ativa Detectada: [ explorer.exe ]", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX, 480, 608, 335, 22, hwnd, (HMENU)IDC_LIVE_TRACKER, NULL, NULL);
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

            // Slider do Jogo Selecionado
            if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_GAME_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_GAME_VIB_VAL), valStr.c_str());
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    g_settings.profiles[g_selectedProfileIndex].vibrance = pos;
                    SaveSettings();
                    RefreshProfileList();

                    // Se este jogo estiver em foco agora, aplicar imediatamente
                    if (g_isGameActive && g_activeGameName == g_settings.profiles[g_selectedProfileIndex].name) {
                        ApplyVibrancePercent(pos);
                    }
                }
            }
            // Slider da Área de Trabalho (Windows Desktop)
            else if (hScroll == GetDlgItem(hwnd, IDC_SLIDER_DESKTOP_VIB)) {
                SetWindowTextW(GetDlgItem(hwnd, IDC_LBL_DESKTOP_VIB_VAL), valStr.c_str());
                g_settings.windowsVibrance = pos;
                SaveSettings();

                // Se nenhum jogo estiver em foco, aplicar imediatamente ao desktop
                if (!g_isGameActive) {
                    ApplyVibrancePercent(pos);
                    UpdateStatusUI(L"Área de Trabalho (Saturação: " + std::to_wstring(pos) + L"%)", false);
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
            // Checkbox Modo Janela
            else if (wmId == IDC_CHK_WINDOWED) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    bool chk = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_WINDOWED), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_settings.profiles[g_selectedProfileIndex].applyWindowed = chk;
                    SaveSettings();
                }
            }
            // Checkbox Perfil Habilitado
            else if (wmId == IDC_CHK_PROFILE_ENABLED) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    bool chk = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_PROFILE_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_settings.profiles[g_selectedProfileIndex].enabled = chk;
                    SaveSettings();
                    RefreshProfileList();
                }
            }
            // Botão Adicionar Jogo
            else if (wmId == IDC_BTN_ADD_GAME) {
                GameProfile newGame = {L"Novo Jogo", L"jogo.exe", 100, true, true};
                g_settings.profiles.push_back(newGame);
                g_selectedProfileIndex = (int)g_settings.profiles.size() - 1;
                SaveSettings();
                RefreshProfileList();
                LoadSelectedProfileToInputs();
            }
            // Botão Remover Jogo
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
            // Botão Detectar Processos Abertos
            else if (wmId == IDC_BTN_DETECT_RUNNING) {
                ShowRunningProcessesDialog(hwnd);
            }
            // Botão Procurar Executável
            else if (wmId == IDC_BTN_BROWSE_EXE) {
                BrowseExeFile(hwnd);
            }
            // Botão Testar Saturação do Jogo
            else if (wmId == IDC_BTN_TEST_GAME_VIB) {
                if (g_selectedProfileIndex >= 0 && g_selectedProfileIndex < (int)g_settings.profiles.size()) {
                    int vib = g_settings.profiles[g_selectedProfileIndex].vibrance;
                    ApplyVibrancePercent(vib);
                    UpdateStatusUI(L"TESTE: " + g_settings.profiles[g_selectedProfileIndex].name + L" (" + std::to_wstring(vib) + L"%)", true);
                }
            }
            // Botão Restaurar Desktop
            else if (wmId == IDC_BTN_TEST_DESKTOP_VIB) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                g_isGameActive = false;
                UpdateStatusUI(L"Área de Trabalho (Saturação: " + std::to_wstring(g_settings.windowsVibrance) + L"%)", false);
            }
            // Checkbox Autostart
            else if (wmId == IDC_CHK_AUTOSTART) {
                g_settings.startWithWindows = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveSettings();
            }
            // Checkbox Minimizar para Bandeja
            else if (wmId == IDC_CHK_MINIMIZE_TRAY) {
                g_settings.minimizeToTrayOnClose = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_MINIMIZE_TRAY), BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveSettings();
            }
            // Menu da Bandeja
            else if (wmId == IDM_TRAY_OPEN) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            else if (wmId == IDM_TRAY_RESTORE_WIN) {
                ApplyVibrancePercent(g_settings.windowsVibrance);
                g_isGameActive = false;
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

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetBkColor(hdcEdit, RGB(34, 34, 46));
            SetTextColor(hdcEdit, RGB(255, 255, 255));
            return (LRESULT)g_hBrushInput;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = (HDC)wParam;
            SetBkColor(hdcList, RGB(26, 26, 36));
            SetTextColor(hdcList, RGB(240, 240, 245));
            return (LRESULT)g_hBrushCard;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            SetBkMode(hdcStatic, TRANSPARENT);

            if (hCtrl == GetDlgItem(hwnd, IDC_LBL_GAME_VIB_VAL)) {
                SetTextColor(hdcStatic, RGB(56, 189, 248)); // Azul Céu
            } else if (hCtrl == GetDlgItem(hwnd, IDC_LBL_DESKTOP_VIB_VAL)) {
                SetTextColor(hdcStatic, RGB(167, 139, 250)); // Roxo Suave
            } else if (hCtrl == GetDlgItem(hwnd, IDC_STATUS_BADGE)) {
                SetTextColor(hdcStatic, g_isGameActive ? RGB(56, 189, 248) : RGB(74, 222, 128));
            } else if (hCtrl == GetDlgItem(hwnd, IDC_LIVE_TRACKER) || hCtrl == GetDlgItem(hwnd, IDC_GPU_LABEL)) {
                SetTextColor(hdcStatic, RGB(156, 163, 175));
            } else {
                SetTextColor(hdcStatic, RGB(235, 235, 242));
            }

            return (LRESULT)g_hBrushCard;
        }

        case WM_CLOSE:
            if (g_settings.minimizeToTrayOnClose) {
                ShowWindow(hwnd, SW_HIDE);
                g_nid.uFlags |= NIF_INFO;
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

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"VibranceApp - Controle de Saturação para Jogos",
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

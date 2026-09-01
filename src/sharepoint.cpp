// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sharepoint.h"
#include "sharepoint.rh2"
#include "dialogs.h"
#include "sharepoint_auth.h"
#include "sharepoint_cache.h"
#include "sharepoint_log.h"
#include "sharepointdarkmode.h"
#include "dialog_find.h"
#include "sharepoint_fs.h"
#include "versinfo.rh2"

// Globals
HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
CSalamanderGUIAbstract* SalamanderGUI = NULL;
int SalamanderVersion = 0;
unsigned char* LowerCase = NULL;
unsigned char* UpperCase = NULL;

const char* PluginNameEN = "Microsoft SharePoint";
const char* PluginNameShort = "SHAREPOINT";

char AssignedFSName[MAX_PATH] = "sharepoint";
int AssignedFSNameLen = 10;

CPluginInterface PluginInterface;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&initCtrls);
    }
    return TRUE;
}

char* LoadStr(int resID)
{
    if (SalamanderGeneral != NULL && HLanguage != NULL)
        return SalamanderGeneral->LoadStr(HLanguage, resID);
    static char buf[8][1024];
    static int idx = 0;
    idx = (idx + 1) & 7;
    buf[idx][0] = 0;
    LoadStringA(HLanguage ? HLanguage : DLLInstance, resID, buf[idx], sizeof(buf[idx]));
    return buf[idx];
}

extern "C" {

int WINAPI SalamanderPluginGetReqVer()
{
    return 103;
}

int WINAPI SalamanderPluginGetSDKVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    if (salamander == NULL) return NULL;

    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();

    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), PluginNameEN);
    if (HLanguage == NULL)
        HLanguage = DLLInstance;

    if (SalamanderVersion < 102)
    {
        MessageBoxA(salamander->GetParentWindow(),
                    "Open Salamander 5.0 or newer is required.",
                    PluginNameEN, MB_OK | MB_ICONERROR);
        return NULL;
    }

    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderGeneral->GetLowerAndUpperCase(&LowerCase, &UpperCase);
    SalamanderGUI = salamander->GetSalamanderGUI();

    if (!InitializeWinLib(PluginNameEN, DLLInstance))
        return NULL;

    BOOL useDark = FALSE;
    if (SalamanderGeneral->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &useDark, sizeof(useDark), NULL))
    {
        PluginDarkMode_SetHostPolicyAvailable(TRUE, useDark);
    }
    SharePointDarkMode::InitTheme();

    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME),
                                   FUNCTION_FILESYSTEM | FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   VERSINFO_VERSION_NO_PLATFORM,
                                   VERSINFO_COPYRIGHT,
                                   LoadStr(IDS_PLUGIN_DESCRIPTION),
                                   PluginNameShort,
                                   NULL,
                                   "sharepoint");

    SalamanderGeneral->GetPluginFSName(AssignedFSName, 0);
    AssignedFSNameLen = (int)strlen(AssignedFSName);

    return &PluginInterface;
}

} // extern "C"

void WINAPI CPluginInterface::About(HWND parent)
{
    OnAbout(parent);
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    CSharePointFindDialog::CloseIfOpen();
    SharePointCache::CacheManager::GetInstance().SaveToDisk();
    SharePointDarkMode::ReleaseTheme();
    return TRUE;
}

void WINAPI CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (registry != NULL && regKey != NULL)
    {
        DWORD val = 1;
        if (registry->GetValue(regKey, "DebugLogging", REG_DWORD, &val, sizeof(val)))
        {
            SharePointLog::SetFileLoggingEnabled(val != 0);
        }
    }
}

void WINAPI CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    if (registry != NULL && regKey != NULL)
    {
        DWORD val = SharePointLog::IsFileLoggingEnabled() ? 1 : 0;
        registry->SetValue(regKey, "DebugLogging", REG_DWORD, &val, sizeof(val));
    }
}

void WINAPI CPluginInterface::Configuration(HWND parent)
{
    OnConfiguration(parent);
}

void WINAPI CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    if (!salamander) return;

    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONNECT), 0, CM_OPEN_SHAREPOINT, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_DISCONNECT), 0, CM_DISCONNECT, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CONFIG), 0, CM_CONFIG, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_ABOUT), 0, CM_ABOUT, FALSE,
                            MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);

    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    if (iconList)
    {
        iconList->Create(16, 16, 1);
        HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_SHAREPOINT), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
        iconList->ReplaceIcon(0, hIcon);
        DestroyIcon(hIcon);
        salamander->SetIconListForGUI(iconList);
    }

    salamander->SetChangeDriveMenuItem("\tMicrosoft SharePoint", 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract* WINAPI CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

extern CPluginInterfaceForFS InterfaceForFS;

CPluginInterfaceForFSAbstract* WINAPI CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}

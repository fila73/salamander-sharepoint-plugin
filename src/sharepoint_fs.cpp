// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sharepoint_fs.h"
#include "sharepoint_auth.h"
#include "sharepoint_api.h"
#include "sharepoint_cache.h"
#include "sharepoint_http.h"
#include "sharepoint_log.h"
#include "dialogs.h"
#include "dialog_find.h"

CPluginInterfaceForFS InterfaceForFS;

CPluginFS::CPluginFS(const char* fsName)
    : m_fsName(fsName ? fsName : "sharepoint"),
      m_currentPath("\\")
{
}

CPluginFS::~CPluginFS()
{
}

BOOL WINAPI CPluginFS::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    detach = FALSE;
    return TRUE;
}

void CPluginFS::Event(int event, DWORD param)
{
}

void CPluginFS::ReleaseObject(HWND parent)
{
}

DWORD WINAPI CPluginFS::GetSupportedServices()
{
    return FS_SERVICE_COPYFROMFS |
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_QUICKRENAME |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETPATHFORMAINWNDTITLE |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_OPENFINDDLG |
           FS_SERVICE_CALCULATEOCCUPIEDSPACE |
           FS_SERVICE_ACCEPTSCHANGENOTIF;
}

BOOL WINAPI CPluginFS::GetChangeDriveOrDisconnectItem(const char* fsName, char*& title, HICON& icon, BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    icon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_SHAREPOINT), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    title = (char*)SalamanderGeneral->Alloc(64);
    if (title)
    {
        snprintf(title, 64, "%s\tMicrosoft SharePoint\t%s", fsName, SharePointAuth::AuthManager::GetInstance().GetAccountDisplay().c_str());
        return TRUE;
    }
    return FALSE;
}

HICON WINAPI CPluginFS::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_SHAREPOINT), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
}

void WINAPI CPluginFS::GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                     DWORD allowedEffects, DWORD keyState, DWORD* dropEffect)
{
    if (dropEffect) *dropEffect = DROPEFFECT_COPY;
}

void WINAPI CPluginFS::GetFSFreeSpace(CQuadWord* retValue)
{
    if (!retValue) return;

    static SharePointApi::StorageQuota cachedQuota;
    static DWORD lastFetchTime = 0;
    DWORD now = GetTickCount();

    if (now - lastFetchTime > 60000 || lastFetchTime == 0)
    {
        if (SharePointApi::ApiClient::GetInstance().GetStorageQuota(cachedQuota))
        {
            lastFetchTime = now;
        }
    }

    if (cachedQuota.remaining > 0)
    {
        retValue->SetUI64((unsigned __int64)cachedQuota.remaining);
    }
    else if (cachedQuota.limit > 0)
    {
        int64_t freeBytes = cachedQuota.limit - cachedQuota.usage;
        if (freeBytes < 0) freeBytes = 0;
        retValue->SetUI64((unsigned __int64)freeBytes);
    }
    else
    {
        *retValue = CQuadWord(-1, -1);
    }
}

void WINAPI CPluginFS::ShowInfoDialog(const char* fsName, HWND parent)
{
    SharePointApi::StorageQuota quota;
    std::string err;
    if (!SharePointApi::ApiClient::GetInstance().GetStorageQuota(quota, &err))
    {
        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return;
    }

    char msg[1024] = {0};
    double usedGB = (double)quota.usage / (1024.0 * 1024.0 * 1024.0);
    double totalGB = (double)quota.limit / (1024.0 * 1024.0 * 1024.0);

    auto& auth = SharePointAuth::AuthManager::GetInstance();
    if (quota.limit > 0)
    {
        snprintf(msg, sizeof(msg),
                 LoadStr(IDS_STORAGE_INFO_FMT),
                 auth.GetTokens().accountName.c_str(),
                 auth.GetTokens().accountEmail.c_str(),
                 usedGB, totalGB, (usedGB / totalGB) * 100.0,
                 (double)quota.deleted / (1024.0 * 1024.0));
    }
    else
    {
        snprintf(msg, sizeof(msg),
                 LoadStr(IDS_STORAGE_UNLIMITED_FMT),
                 auth.GetTokens().accountName.c_str(),
                 auth.GetTokens().accountEmail.c_str(),
                 usedGB,
                 (double)quota.deleted / (1024.0 * 1024.0));
    }

    SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginFS::GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return FALSE;

    std::string acc = SharePointAuth::AuthManager::GetInstance().GetTokens().accountEmail;
    if (acc.empty()) acc = "SharePoint";

    std::string displayPath = m_currentPath;
    if (displayPath.empty()) displayPath = "\\";

    snprintf(buf, bufSize, "%s [%s]: %s", fsName, acc.c_str(), displayPath.c_str());
    return TRUE;
}

BOOL WINAPI CPluginFS::GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset)
{
    return FALSE;
}

void WINAPI CPluginFS::CompleteDirectoryLineHotPath(char* path, int pathBufSize)
{
}

void WINAPI CPluginFS::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
    SharePointCache::CacheManager::GetInstance().InvalidateAll();
}

BOOL WINAPI CPluginFS::ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs)
{
    return FALSE;
}

void WINAPI CPluginFS::OpenActiveFolder(const char* fsName, HWND parent)
{
}

void WINAPI CPluginFS::GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects)
{
    if (allowedEffects) *allowedEffects = DROPEFFECT_COPY;
}

BOOL WINAPI CPluginFS::GetNoItemsInPanelText(char* textBuf, int textBufSize)
{
    return FALSE;
}

void WINAPI CPluginFS::ShowSecurityInfo(HWND parent)
{
}

CPluginFSInterfaceAbstract* WINAPI CPluginInterfaceForFS::OpenFS(const char* fsName, int fsNameIndex)
{
    if (!SharePointAuth::AuthManager::GetInstance().IsAuthenticated())
    {
        std::string err;
        if (!SharePointAuth::AuthManager::GetInstance().LaunchInteractiveAuth(NULL, &err))
        {
            if (!err.empty())
            {
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return NULL;
        }
    }

    return new (std::nothrow) CPluginFS(fsName);
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    if (fs)
    {
        delete static_cast<CPluginFS*>(fs);
    }
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    int failReason = 0;
    SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName, "\\", &failReason);
}

BOOL WINAPI CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                                 CPluginFSInterfaceAbstract* pluginFS,
                                                                 const char* pluginFSName, int pluginFSNameIndex,
                                                                 BOOL isDetachedFS, BOOL& refreshMenu,
                                                                 BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    return FALSE;
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
}

void WINAPI CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                              const char* pluginFSName, int pluginFSNameIndex,
                                              CFileData& file, int isDir)
{
    CPluginFS* fs = static_cast<CPluginFS*>(pluginFS);
    if (!fs) return;

    if (isDir)
    {
        char newPath[MAX_PATH];
        lstrcpynA(newPath, fs->GetCurrentPathStr().c_str(), MAX_PATH);

        if (isDir == 2) // ".."
        {
            char* cutDir = NULL;
            if (SalamanderGeneral->CutDirectory(newPath, &cutDir))
            {
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath, NULL,
                                                             -1, cutDir);
            }
        }
        else // subdirectory
        {
            if (newPath[0] == '\0') strcpy(newPath, "\\");
            if (newPath[strlen(newPath) - 1] != '\\') strcat(newPath, "\\");
            if (file.Name) strncat(newPath, file.Name, MAX_PATH - strlen(newPath) - 1);

            SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath);
        }
    }
}

BOOL WINAPI CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                                CPluginFSInterfaceAbstract* pluginFS,
                                                const char* pluginFSName, int pluginFSNameIndex)
{
    if (isInPanel)
    {
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
        return SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
    }
    else
    {
        return SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
    }
}

void WINAPI CPluginInterfaceForFS::ConvertPathToInternal(const char* fsName, int fsNameIndex, char* fsUserPart)
{
    if (!fsUserPart) return;
    for (char* p = fsUserPart; *p; ++p)
    {
        if (*p == '/') *p = '\\';
    }
}

void WINAPI CPluginInterfaceForFS::ConvertPathToExternal(const char* fsName, int fsNameIndex, char* fsUserPart)
{
    if (!fsUserPart) return;
    for (char* p = fsUserPart; *p; ++p)
    {
        if (*p == '/') *p = '\\';
    }
}

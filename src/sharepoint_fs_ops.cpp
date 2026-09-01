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

BOOL WINAPI CPluginFS::CreateDir(const char* fsName, int mode, HWND parent,
                                 char* newName, BOOL& cancel)
{
    cancel = FALSE;

    if (mode == 1)
    {
        return FALSE;
    }

    if (mode == 2)
    {
        if (!newName || !*newName) return FALSE;

        std::string driveId, folderItemId, siteId;
        ResolveCurrentTarget(driveId, folderItemId, siteId);
        if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

        if (driveId.empty())
        {
            SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_CREATE_DIR_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return FALSE;
        }

        SharePointApi::SharePointItem createdDir;
        std::string err;
        if (!SharePointApi::ApiClient::GetInstance().CreateFolder(driveId, folderItemId, newName, createdDir, &err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return FALSE;
        }

        SharePointCache::CacheManager::GetInstance().InvalidateAll();
        SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
        SalamanderGeneral->PostRefreshPanelFS(this);
        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI CPluginFS::QuickRename(const char* fsName, int mode, HWND parent,
                                   CFileData& file, BOOL isDir, char* newName, BOOL& cancel)
{
    cancel = FALSE;

    if (mode == 1)
    {
        return FALSE;
    }

    if (mode == 2)
    {
        if (!newName || !*newName) return FALSE;

        std::string itemId = FindItemByPanelName(file.Name, isDir);
        std::string driveId, folderItemId, siteId;
        ResolveCurrentTarget(driveId, folderItemId, siteId);
        if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

        if (itemId.empty())
        {
            SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_GET_ITEM_ID), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return FALSE;
        }

        std::string err;
        if (!SharePointApi::ApiClient::GetInstance().RenameItem(driveId, itemId, newName, &err))
        {
            SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return FALSE;
        }

        SharePointCache::CacheManager::GetInstance().InvalidateAll();
        SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
        SalamanderGeneral->PostRefreshPanelFS(this);
        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI CPluginFS::Delete(const char* fsName, int mode, HWND parent, int panel,
                              int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;

    if (mode == 1)
    {
        return FALSE;
    }

    if (mode == 2)
    {
        std::string driveId, folderItemId, siteId;
        ResolveCurrentTarget(driveId, folderItemId, siteId);
        if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

        bool anyFailed = false;
        BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
        int index = 0;
        BOOL isDir = FALSE;

        while (true)
        {
            const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                         : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
            if (f == NULL) break;

            if (f->Name && strcmp(f->Name, "..") != 0)
            {
                std::string itemId = FindItemByPanelName(f->Name, isDir);
                if (!itemId.empty())
                {
                    std::string err;
                    if (!SharePointApi::ApiClient::GetInstance().DeleteItem(driveId, itemId, &err))
                    {
                        anyFailed = true;
                        SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                    }
                }
            }

            if (focused) break;
        }

        SharePointCache::CacheManager::GetInstance().InvalidateAll();
        SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
        SalamanderGeneral->PostRefreshPanelFS(this);
        return !anyFailed;
    }

    return FALSE;
}

void WINAPI CPluginFS::ShowProperties(const char* fsName, HWND parent, int panel,
                                      int selectedFiles, int selectedDirs)
{
    BOOL isDir = FALSE;
    const CFileData* file = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    if (!file || !file->Name) return;

    std::string itemId = FindItemByPanelName(file->Name, isDir);
    std::string driveId, folderItemId, siteId;
    ResolveCurrentTarget(driveId, folderItemId, siteId);
    if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

    SharePointApi::SharePointItem item;
    std::string err;
    if (SharePointApi::ApiClient::GetInstance().GetItemMetadata(driveId, itemId, item, &err))
    {
        char msg[1024] = {0};
        snprintf(msg, sizeof(msg),
                 LoadStr(IDS_PROPERTIES_FMT),
                 item.name.c_str(),
                 item.size,
                 item.authorName.c_str(),
                 item.lastModifyingUserName.c_str(),
                 item.webUrl.c_str());
        SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PROPERTIES_TITLE), MB_OK | MB_ICONINFORMATION);
    }
}

void WINAPI CPluginFS::ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                  int panel, int selectedFiles, int selectedDirs)
{
}

BOOL WINAPI CPluginFS::HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    return FALSE;
}

BOOL WINAPI CPluginFS::OpenFindDialog(const char* fsName, int panel)
{
    CSharePointFindDialog::Launch(SalamanderGeneral->GetMainWindowHWND(), panel, m_currentPath);
    return TRUE;
}

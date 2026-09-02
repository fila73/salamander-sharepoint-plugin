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

static void PrecalculateLocalItems(const std::wstring& localPath, bool isDir, int& totalCount, int64_t& totalBytes)
{
    if (!isDir)
    {
        totalCount++;
        HANDLE hF = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hF != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER li;
            if (GetFileSizeEx(hF, &li))
            {
                totalBytes += li.QuadPart;
            }
            CloseHandle(hF);
        }
        return;
    }

    std::wstring searchPattern = localPath;
    if (!searchPattern.empty() && searchPattern.back() != L'\\') searchPattern += L"\\";
    searchPattern += L"*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            std::wstring subPath = localPath;
            if (!subPath.empty() && subPath.back() != L'\\') subPath += L"\\";
            subPath += fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                PrecalculateLocalItems(subPath, true, totalCount, totalBytes);
            }
            else
            {
                totalCount++;
                LARGE_INTEGER li;
                li.LowPart = fd.nFileSizeLow;
                li.HighPart = fd.nFileSizeHigh;
                totalBytes += li.QuadPart;
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

BOOL WINAPI CPluginFS::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                              const char* sourcePath, SalEnumSelection2 next,
                                              void* nextParam, int selectedFiles, int selectedDirs,
                                              char* targetPath, BOOL* cancelOrHandlePath)
{
    if (cancelOrHandlePath) *cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        if (targetPath)
        {
            std::string fsPrefix = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":\\";
            std::string dest = fsPrefix;
            if (!m_currentPath.empty() && m_currentPath != "\\")
            {
                std::string rel = m_currentPath;
                if (rel[0] == '\\') rel = rel.substr(1);
                dest += rel;
            }
            else
            {
                dest += "OneDrive";
            }
            strncpy(targetPath, dest.c_str(), 2 * MAX_PATH - 1);
            targetPath[2 * MAX_PATH - 1] = '\0';
        }
        SalamanderGeneral->SalPathAppend(targetPath, "*.*", 2 * MAX_PATH);
        return TRUE;
    }

    if (mode == 4)
    {
        return FALSE;
    }

    std::string driveId, folderItemId, siteId;
    ResolveCurrentTarget(driveId, folderItemId, siteId);
    if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

    if (driveId.empty())
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_ERR_CANNOT_UPLOAD_ROOT), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    std::wstring wSourcePath = SharePointHttp::HttpClient::AnsiToWide(sourcePath ? sourcePath : "");
    if (!wSourcePath.empty() && wSourcePath.back() != L'\\')
        wSourcePath += L"\\";

    const char* itemName = NULL;
    int isDir = 0;
    CQuadWord fileSize;
    DWORD attr = 0;
    FILETIME lastWriteTime = {0, 0};
    int enumError = 0;

    int totalCount = 0;
    int64_t totalBytes = 0;

    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0) continue;

        std::wstring itemLocalPath = wSourcePath + SharePointHttp::HttpClient::AnsiToWide(itemName);
        PrecalculateLocalItems(itemLocalPath, isDir != 0, totalCount, totalBytes);
    }

    next(parent, -1, NULL, NULL, NULL, NULL, NULL, nextParam, NULL);

    auto progressDlg = CDialogTransferProgress::Create(parent, false);
    progressDlg->SetTotalItemsCount(totalCount);
    progressDlg->SetTotalBatchBytes(totalBytes);

    int64_t batchUploadedBytes = 0;

    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0) continue;

        if (progressDlg->IsCancelled())
        {
            break;
        }

        std::wstring itemLocalPath = wSourcePath + SharePointHttp::HttpClient::AnsiToWide(itemName);

        if (isDir)
        {
            std::string folderName = itemName;
            SharePointApi::SharePointItem createdDir;
            std::string err;
            SharePointApi::ApiClient::GetInstance().CreateFolder(driveId, folderItemId, folderName, createdDir, &err);
        }
        else
        {
            std::string fileName = itemName;
            progressDlg->SetCurrentFileName(fileName);

            auto progCb = [&](int64_t bytesTransferred, int64_t totalFileBytes) -> bool {
                progressDlg->UpdateFileProgress(bytesTransferred, totalFileBytes);
                progressDlg->UpdateBatchProgress(batchUploadedBytes + bytesTransferred, totalBytes);
                return !progressDlg->IsCancelled();
            };

            SharePointApi::SharePointItem uploadedItem;
            std::string err;
            bool ok = SharePointApi::ApiClient::GetInstance().UploadFile(driveId, folderItemId, itemLocalPath, fileName,
                                                                         progCb, progressDlg->GetCancelFlagPtr(),
                                                                         &uploadedItem, &err);
            if (ok)
            {
                LARGE_INTEGER li;
                HANDLE hF = CreateFileW(itemLocalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                if (hF != INVALID_HANDLE_VALUE)
                {
                    if (GetFileSizeEx(hF, &li)) batchUploadedBytes += li.QuadPart;
                    CloseHandle(hF);
                }
                progressDlg->IncrementCompletedItems();

                if (!copy)
                {
                    DeleteFileW(itemLocalPath.c_str());
                }
            }
            else if (!progressDlg->IsCancelled())
            {
                SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
        }
    }

    progressDlg->Close();

    if (progressDlg->IsCancelled())
    {
        if (cancelOrHandlePath) *cancelOrHandlePath = TRUE;
        return TRUE;
    }

    SharePointCache::CacheManager::GetInstance().InvalidateAll();
    SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
    SalamanderGeneral->PostRefreshPanelFS(this);
    return TRUE;
}

BOOL WINAPI CPluginFS::CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                       int panel, int selectedFiles, int selectedDirs,
                                       char* targetPath, BOOL& operationMask,
                                       BOOL& cancelOrHandlePath, HWND dropTarget)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;

    if (mode == 1)
    {
        if (targetPath && targetPath[0] == 0)
        {
            int otherPanel = (panel == PANEL_SOURCE) ? PANEL_TARGET : PANEL_SOURCE;
            SalamanderGeneral->GetPanelPath(otherPanel, targetPath, MAX_PATH, NULL, NULL, 0);
        }
        return FALSE;
    }
    if (mode == 4)
    {
        return FALSE;
    }

    if (!targetPath || !*targetPath) return FALSE;
    std::wstring wTargetDir = SharePointHttp::HttpClient::AnsiToWide(targetPath);

    std::string driveId, folderItemId, siteId;
    ResolveCurrentTarget(driveId, folderItemId, siteId);
    if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

    auto progressDlg = CDialogTransferProgress::Create(parent, true);

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;

    int totalCount = 0;
    int64_t totalBytes = 0;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL) break;
        if (!isDir && f->Name)
        {
            totalCount++;
            totalBytes += (int64_t)f->Size.Value;
        }
        if (focused) break;
    }

    progressDlg->SetTotalItemsCount(totalCount);
    progressDlg->SetTotalBatchBytes(totalBytes);

    int64_t batchDownloadedBytes = 0;
    index = 0;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL) break;

        if (progressDlg->IsCancelled()) break;

        if (!isDir && f->Name)
        {
            std::string fileName = f->Name;
            SharePointApi::SharePointItem item;
            const auto* pItem = FindItemObjectByPanelName(f->Name, FALSE);
            if (pItem)
            {
                item = *pItem;
                if (item.driveId.empty()) item.driveId = driveId;
            }
            else
            {
                item.id = FindItemByPanelName(f->Name, FALSE);
                item.driveId = driveId;
                item.name = fileName;
                item.size = (int64_t)f->Size.Value;
            }

            progressDlg->SetCurrentFileName(fileName);

            std::wstring destFile = wTargetDir;
            if (!destFile.empty() && destFile.back() != L'\\') destFile += L"\\";
            destFile += SharePointHttp::HttpClient::SanitizeFileNameForLocalFsW(SharePointHttp::HttpClient::Utf8ToWide(fileName));

            auto progCb = [&](int64_t bytesTransferred, int64_t totalFileBytes) -> bool {
                progressDlg->UpdateFileProgress(bytesTransferred, totalFileBytes);
                progressDlg->UpdateBatchProgress(batchDownloadedBytes + bytesTransferred, totalBytes);
                return !progressDlg->IsCancelled();
            };

            std::string err;
            bool ok = SharePointApi::ApiClient::GetInstance().DownloadFile(item, destFile, progCb,
                                                                           progressDlg->GetCancelFlagPtr(), &err);
            if (ok)
            {
                batchDownloadedBytes += item.size;
                progressDlg->IncrementCompletedItems();

                if (!copy)
                {
                    SharePointApi::ApiClient::GetInstance().DeleteItem(driveId, item.id);
                }
            }
            else if (!progressDlg->IsCancelled())
            {
                progressDlg->Close();
                SalamanderGeneral->SalMessageBox(parent, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                cancelOrHandlePath = TRUE;
                return TRUE;
            }
        }

        if (focused) break;
    }

    progressDlg->Close();

    if (progressDlg->IsCancelled())
    {
        cancelOrHandlePath = TRUE;
        return TRUE;
    }

    if (!copy)
    {
        SharePointCache::CacheManager::GetInstance().InvalidateAll();
        SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
        SalamanderGeneral->PostRefreshPanelFS(this);
    }

    return TRUE;
}

void WINAPI CPluginFS::ViewFile(const char* fsName, HWND parent,
                                CSalamanderForViewFileOnFSAbstract* salamander,
                                CFileData& file)
{
    if (!file.Name || !salamander) return;

    std::string driveId, folderItemId, siteId;
    ResolveCurrentTarget(driveId, folderItemId, siteId);
    if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

    SharePointApi::SharePointItem targetItem;
    const auto* pItem = FindItemObjectByPanelName(file.Name, FALSE);
    if (pItem)
    {
        targetItem = *pItem;
        if (targetItem.driveId.empty()) targetItem.driveId = driveId;
    }
    else
    {
        targetItem.id = FindItemByPanelName(file.Name, FALSE);
        targetItem.driveId = driveId;
        targetItem.name = file.Name;
        targetItem.size = (int64_t)file.Size.Value;
    }

    std::string uniqueFileName = std::string(fsName ? fsName : "sharepoint") + ":" + m_currentPath + "\\" + targetItem.id + "_" + targetItem.name;

    BOOL fileExists = FALSE;
    const char* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), file.Name, NULL, fileExists);
    if (!tmpFileName) return;

    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);

    if (!fileExists)
    {
        std::wstring wTempPath = SharePointHttp::HttpClient::AnsiToWide(tmpFileName);
        std::string err;
        newFileOK = SharePointApi::ApiClient::GetInstance().DownloadFile(targetItem, wTempPath, nullptr, nullptr, &err);
        if (!newFileOK)
        {
            salamander->FreeFileNameInCache(uniqueFileName.c_str(), FALSE, FALSE, newFileSize, NULL, FALSE, TRUE);
            SalamanderGeneral->SalMessageBox(parent, err.empty() ? "Download failed" : err.c_str(),
                                             LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            return;
        }

        HANDLE hF = CreateFileW(wTempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hF != INVALID_HANDLE_VALUE)
        {
            DWORD dwHigh = 0;
            DWORD dwLow = GetFileSize(hF, &dwHigh);
            newFileSize = CQuadWord(dwLow, dwHigh);
            CloseHandle(hF);
        }
    }

    HANDLE fileLock = NULL;
    BOOL fileLockOwner = FALSE;
    salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner);
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK, newFileSize, fileLock, fileLockOwner, FALSE);
}

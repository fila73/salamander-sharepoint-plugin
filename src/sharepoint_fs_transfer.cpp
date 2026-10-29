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
#include "sharepoint_transfer_logic.h"

using SharePointTransfer::ConflictChoice;
using SharePointTransfer::ConflictState;

// Asks the user what to do with an already existing item (Overwrite / Skip / Cancel,
// with "Apply to all" giving Overwrite all / Skip all). Honors a remembered batch decision.
static ConflictChoice AskConflict(HWND parent, ConflictState& state, const std::string& displayName, bool isUpload)
{
    if (state.hasAll) return state.all;

    char text[2 * MAX_PATH + 512];
    std::string fmt = LoadStr(isUpload ? IDS_XFER_CONFLICT_UPLOAD_FMT : IDS_XFER_CONFLICT_DOWNLOAD_FMT);
    snprintf(text, sizeof(text), fmt.c_str(), displayName.c_str());
    std::string title = LoadStr(IDS_XFER_CONFLICT_TITLE);
    std::string checkText = LoadStr(IDS_XFER_APPLY_ALL);
    std::string btnOverwrite = LoadStr(IDS_XFER_BTN_OVERWRITE);
    std::string btnSkip = LoadStr(IDS_XFER_BTN_SKIP);
    std::string alias = std::to_string(IDYES) + "\t" + btnOverwrite + "\t" + std::to_string(IDNO) + "\t" + btnSkip;

    BOOL applyAll = FALSE;
    MSGBOXEX_PARAMS p = {};
    p.HParent = parent;
    p.Text = text;
    p.Caption = title.c_str();
    p.Flags = MSGBOXEX_YESNOCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_DEFBUTTON2 | MSGBOXEX_ESCAPEENABLED;
    p.CheckBoxText = checkText.c_str();
    p.CheckBoxValue = &applyAll;
    p.AliasBtnNames = alias.c_str();
    int res = SalamanderGeneral->SalMessageBoxEx(&p);

    ConflictChoice c = ConflictChoice::Cancel;
    if (res == IDYES) c = ConflictChoice::Overwrite;
    else if (res == IDNO) c = ConflictChoice::Skip;
    state.Remember(c, applyAll != FALSE);
    return c;
}

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

static bool EnsureLocalDirectoryExists(const std::wstring& dirPath)
{
    if (dirPath.empty()) return false;
    DWORD attrs = GetFileAttributesW(dirPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    size_t pos = 0;
    while ((pos = dirPath.find_first_of(L"\\/", pos + 1)) != std::wstring::npos)
    {
        std::wstring sub = dirPath.substr(0, pos);
        if (sub.length() > 2 && sub.back() != L':')
        {
            CreateDirectoryW(sub.c_str(), NULL);
        }
    }
    CreateDirectoryW(dirPath.c_str(), NULL);
    attrs = GetFileAttributesW(dirPath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static void ParseLocalTarget(const std::string& rawTargetPath, int totalItems, const std::string& singleFileName,
                             std::wstring& outTargetDir, std::wstring& outSingleTargetFileName)
{
    std::string s = rawTargetPath;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();

    // Strip wildcards e.g. \*.*, *.*, \*, *
    if (s.length() >= 4 && s.substr(s.length() - 4) == "\\*.*")
    {
        s = s.substr(0, s.length() - 4);
    }
    else if (s.length() >= 3 && s.substr(s.length() - 3) == "*.*")
    {
        s = s.substr(0, s.length() - 3);
        if (!s.empty() && s.back() == '\\') s.pop_back();
    }
    else if (s.length() >= 2 && s.substr(s.length() - 2) == "\\*")
    {
        s = s.substr(0, s.length() - 2);
    }
    else if (s.length() >= 1 && s.back() == '*')
    {
        s.pop_back();
        if (!s.empty() && s.back() == '\\') s.pop_back();
    }

    std::wstring ws = SharePointHttp::HttpClient::AnsiToWide(s);

    if (totalItems == 1 && !singleFileName.empty())
    {
        DWORD attrs = GetFileAttributesW(ws.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            outTargetDir = ws;
            outSingleTargetFileName = L"";
        }
        else
        {
            size_t lastSlash = ws.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos)
            {
                std::wstring parentDir = ws.substr(0, lastSlash);
                std::wstring namePart = ws.substr(lastSlash + 1);
                DWORD parentAttrs = GetFileAttributesW(parentDir.c_str());
                if (parentAttrs != INVALID_FILE_ATTRIBUTES && (parentAttrs & FILE_ATTRIBUTE_DIRECTORY))
                {
                    outTargetDir = parentDir;
                    outSingleTargetFileName = namePart;
                    return;
                }
            }
            outTargetDir = ws;
            outSingleTargetFileName = L"";
        }
    }
    else
    {
        outTargetDir = ws;
        outSingleTargetFileName = L"";
    }
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
        if (targetPath)
        {
            if (targetPath[0] == 0)
            {
                int otherPanel = (panel == PANEL_SOURCE) ? PANEL_TARGET : PANEL_SOURCE;
                SalamanderGeneral->GetPanelPath(otherPanel, targetPath, MAX_PATH, NULL, NULL, 0);
            }
            SalamanderGeneral->SalPathAppend(targetPath, "*.*", MAX_PATH);
        }
        return FALSE;
    }
    if (mode == 4)
    {
        return FALSE;
    }

    if (!targetPath || !*targetPath) return FALSE;

    std::string driveId, folderItemId, siteId;
    ResolveCurrentTarget(driveId, folderItemId, siteId);
    if (driveId.empty() && m_currentPath.rfind("\\OneDrive", 0) == 0) driveId = "me";

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;

    int totalCount = 0;
    int64_t totalBytes = 0;
    std::string firstFileName;

    while (true)
    {
        const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                     : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL) break;
        if (!isDir && f->Name)
        {
            totalCount++;
            totalBytes += (int64_t)f->Size.Value;
            if (firstFileName.empty()) firstFileName = f->Name;
        }
        if (focused) break;
    }

    if (totalCount == 0)
    {
        return TRUE;
    }

    std::wstring wTargetDir, wSingleTargetFileName;
    ParseLocalTarget(targetPath, totalCount, firstFileName, wTargetDir, wSingleTargetFileName);
    EnsureLocalDirectoryExists(wTargetDir);

    auto progressDlg = CDialogTransferProgress::Create(parent, true);
    progressDlg->SetTotalItemsCount(totalCount);
    progressDlg->SetTotalBatchBytes(totalBytes);

    int64_t batchDownloadedBytes = 0;
    ConflictState conflictState;
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

            if (totalCount == 1 && !wSingleTargetFileName.empty())
            {
                destFile += wSingleTargetFileName;
            }
            else
            {
                destFile += SharePointHttp::HttpClient::SanitizeFileNameForLocalFsW(SharePointHttp::HttpClient::Utf8ToWide(fileName));
            }

            if (GetFileAttributesW(destFile.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ConflictChoice choice = AskConflict(parent, conflictState, fileName, false);
                if (choice == ConflictChoice::Cancel)
                {
                    cancelOrHandlePath = TRUE;
                    progressDlg->Close();
                    return TRUE;
                }
                if (choice == ConflictChoice::Skip)
                {
                    if (focused) break;
                    continue; // skipped: not counted as transferred, never deleted remotely
                }
            }

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

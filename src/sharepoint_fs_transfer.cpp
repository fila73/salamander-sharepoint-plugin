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

using SharePointTransfer::ResultNode;

static bool SameRemoteName(const std::string& a, const std::string& b)
{
    return _wcsicmp(SharePointHttp::HttpClient::Utf8ToWide(a).c_str(),
                    SharePointHttp::HttpClient::Utf8ToWide(b).c_str()) == 0;
}

static const SharePointApi::SharePointItem* FindRemoteByName(const std::vector<SharePointApi::SharePointItem>& items,
                                                             const std::string& name)
{
    for (const auto& it : items)
        if (SameRemoteName(it.name, name)) return &it;
    return nullptr;
}

static void ShowTransferError(HWND parent, const std::string& msg)
{
    SalamanderGeneral->SalMessageBox(parent, msg.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
}

static std::string FormatFolderError(const std::string& folder, const std::string& err)
{
    char buf[2 * MAX_PATH + 512];
    std::string fmt = LoadStr(IDS_XFER_ERR_FOLDER_FMT);
    snprintf(buf, sizeof(buf), fmt.c_str(), folder.c_str(), err.c_str());
    return buf;
}

// ---- upload (local -> cloud), recursive ---------------------------------------

struct UploadCtx
{
    HWND parent = NULL;
    std::string driveId;
    std::shared_ptr<CDialogTransferProgress> dlg;
    int64_t totalBytes = 0;
    int64_t uploadedBytes = 0;
    ConflictState conflict;
    bool cancelled = false;
};

static void UploadTree(UploadCtx& ctx, const std::wstring& localPath, const std::string& name, bool isDir,
                       const std::string& parentId, const std::vector<SharePointApi::SharePointItem>& siblings,
                       ResultNode& node)
{
    node.path = localPath;
    node.isDir = isDir;
    node.transferred = false;
    node.dirReady = false;

    if (ctx.dlg->IsCancelled())
    {
        ctx.cancelled = true;
        return;
    }

    auto& api = SharePointApi::ApiClient::GetInstance();
    const SharePointApi::SharePointItem* existing = FindRemoteByName(siblings, name);

    if (isDir)
    {
        std::string folderId, err;
        bool existed = false;
        if (existing && existing->isFolder)
        {
            folderId = existing->id; // merge into the existing remote folder
            existed = true;
        }
        else if (existing)
        {
            ShowTransferError(ctx.parent, FormatFolderError(name, "an item with this name already exists"));
            return;
        }
        else
        {
            SharePointApi::SharePointItem created;
            if (!api.CreateFolder(ctx.driveId, parentId, name, created, &err))
            {
                ShowTransferError(ctx.parent, FormatFolderError(name, err));
                return;
            }
            folderId = created.id;
        }

        std::vector<SharePointApi::SharePointItem> kids;
        if (existed && !api.ListDriveChildren(ctx.driveId, folderId, kids, &err))
        {
            ShowTransferError(ctx.parent, FormatFolderError(name, err));
            return;
        }
        node.dirReady = true;

        std::wstring pattern = localPath + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            // cannot enumerate: the folder is not known to be fully uploaded
            node.dirReady = false;
            return;
        }
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            node.children.emplace_back();
            UploadTree(ctx, localPath + L"\\" + fd.cFileName, SharePointHttp::HttpClient::WideToUtf8(fd.cFileName),
                       (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, folderId, kids, node.children.back());
            if (ctx.cancelled) { node.dirReady = false; break; }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        return;
    }

    ctx.dlg->SetCurrentFileName(name);

    auto progCb = [&](int64_t bytesTransferred, int64_t totalFileBytes) -> bool {
        ctx.dlg->UpdateFileProgress(bytesTransferred, totalFileBytes);
        ctx.dlg->UpdateBatchProgress(ctx.uploadedBytes + bytesTransferred, ctx.totalBytes);
        return !ctx.dlg->IsCancelled();
    };

    SharePointApi::SharePointItem uploadedItem;
    std::string err;
    bool ok = api.UploadFile(ctx.driveId, parentId, localPath, name, progCb, ctx.dlg->GetCancelFlagPtr(),
                             &uploadedItem, &err);
    if (ok)
    {
        WIN32_FILE_ATTRIBUTE_DATA ad;
        if (GetFileAttributesExW(localPath.c_str(), GetFileExInfoStandard, &ad))
            ctx.uploadedBytes += ((int64_t)ad.nFileSizeHigh << 32) | ad.nFileSizeLow;
        ctx.dlg->IncrementCompletedItems();
        node.transferred = true;
    }
    else if (ctx.dlg->IsCancelled())
    {
        ctx.cancelled = true;
    }
    else
    {
        ShowTransferError(ctx.parent, err);
    }
}

// Move: delete local sources only for items that were really uploaded.
static void CleanupLocalMoved(const ResultNode& n)
{
    if (!n.isDir)
    {
        if (n.transferred) DeleteFileW(n.path.c_str());
        return;
    }
    for (const auto& c : n.children) CleanupLocalMoved(c);
    if (SharePointTransfer::ShouldDeleteSource(n)) RemoveDirectoryW(n.path.c_str());
}

// ---- download (cloud -> local), recursive ---------------------------------------

struct DlNode
{
    SharePointApi::SharePointItem item;
    std::vector<DlNode> kids;
};

static bool BuildDlTree(const SharePointApi::SharePointItem& item, DlNode& n, int& files, int64_t& bytes,
                        std::string& err, int depth)
{
    n.item = item;
    if (!item.isFolder)
    {
        files++;
        bytes += item.size;
        return true;
    }
    if (depth > 64)
    {
        err = "Folder nesting too deep";
        return false;
    }
    std::vector<SharePointApi::SharePointItem> kids;
    if (!SharePointApi::ApiClient::GetInstance().ListDriveChildren(item.driveId, item.id, kids, &err)) return false;
    n.kids.reserve(kids.size());
    for (auto& k : kids)
    {
        if (k.driveId.empty()) k.driveId = item.driveId;
        n.kids.emplace_back();
        if (!BuildDlTree(k, n.kids.back(), files, bytes, err, depth + 1)) return false;
    }
    return true;
}

struct DownloadCtx
{
    HWND parent = NULL;
    std::shared_ptr<CDialogTransferProgress> dlg;
    int64_t totalBytes = 0;
    int64_t downloadedBytes = 0;
    ConflictState conflict;
    bool cancelled = false; // user cancelled (also from the conflict dialog)
    bool aborted = false;   // fatal error, already reported
};

static void DownloadTree(DownloadCtx& ctx, const DlNode& n, const std::wstring& localPath, ResultNode& r)
{
    r.isDir = n.item.isFolder;
    r.remoteId = n.item.id;
    r.transferred = false;
    r.dirReady = false;

    if (ctx.dlg->IsCancelled())
    {
        ctx.cancelled = true;
        return;
    }

    if (n.item.isFolder)
    {
        DWORD a = GetFileAttributesW(localPath.c_str());
        if (a == INVALID_FILE_ATTRIBUTES)
        {
            if (!CreateDirectoryW(localPath.c_str(), NULL))
            {
                ShowTransferError(ctx.parent, FormatFolderError(n.item.name, "cannot create local folder"));
                return;
            }
        }
        else if (!(a & FILE_ATTRIBUTE_DIRECTORY))
        {
            ShowTransferError(ctx.parent, FormatFolderError(n.item.name, "a local file with this name exists"));
            return;
        }
        r.dirReady = true;
        for (const auto& k : n.kids)
        {
            r.children.emplace_back();
            ResultNode& cr = r.children.back();
            cr.isDir = k.item.isFolder;
            cr.remoteId = k.item.id;
            std::wstring comp;
            if (!SharePointTransfer::MakeSafeLocalComponent(SharePointHttp::HttpClient::Utf8ToWide(k.item.name), comp))
            {
                char buf[2 * MAX_PATH + 256];
                std::string fmt = LoadStr(IDS_XFER_ERR_UNSAFE_NAME_FMT);
                snprintf(buf, sizeof(buf), fmt.c_str(), k.item.name.c_str());
                ShowTransferError(ctx.parent, buf);
                cr.dirReady = false; // unsafe name: skipped, never deleted remotely
                continue;
            }
            DownloadTree(ctx, k, localPath + L"\\" + comp, cr);
            if (ctx.cancelled || ctx.aborted) { r.dirReady = false; break; }
        }
        return;
    }

    ctx.dlg->SetCurrentFileName(n.item.name);

    if (GetFileAttributesW(localPath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        ConflictChoice choice = AskConflict(ctx.parent, ctx.conflict, n.item.name, false);
        if (choice == ConflictChoice::Cancel)
        {
            ctx.cancelled = true;
            return;
        }
        if (choice == ConflictChoice::Skip) return; // skipped: not transferred, never deleted remotely
    }

    auto progCb = [&](int64_t bytesTransferred, int64_t totalFileBytes) -> bool {
        ctx.dlg->UpdateFileProgress(bytesTransferred, totalFileBytes);
        ctx.dlg->UpdateBatchProgress(ctx.downloadedBytes + bytesTransferred, ctx.totalBytes);
        return !ctx.dlg->IsCancelled();
    };

    std::string err;
    bool ok = SharePointApi::ApiClient::GetInstance().DownloadFile(n.item, localPath, progCb,
                                                                   ctx.dlg->GetCancelFlagPtr(), &err);
    if (ok)
    {
        ctx.downloadedBytes += n.item.size;
        ctx.dlg->IncrementCompletedItems();
        r.transferred = true;
    }
    else if (ctx.dlg->IsCancelled())
    {
        ctx.cancelled = true;
    }
    else
    {
        ctx.dlg->Close();
        ShowTransferError(ctx.parent, err);
        ctx.aborted = true;
    }
}

// Move: delete a remote item only if everything below it was downloaded.
static void CleanupRemoteMoved(const std::string& driveId, const ResultNode& n)
{
    if (SharePointTransfer::ShouldDeleteSource(n))
    {
        SharePointApi::ApiClient::GetInstance().DeleteItem(driveId, n.remoteId);
        return;
    }
    if (n.isDir)
        for (const auto& c : n.children) CleanupRemoteMoved(driveId, c);
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

    UploadCtx ctx;
    ctx.parent = parent;
    ctx.driveId = driveId;
    ctx.dlg = progressDlg;
    ctx.totalBytes = totalBytes;

    std::vector<SharePointApi::SharePointItem> siblings;
    std::string listErr;
    if (!SharePointApi::ApiClient::GetInstance().ListDriveChildren(driveId, folderItemId, siblings, &listErr))
    {
        progressDlg->Close();
        ShowTransferError(parent, listErr);
        return FALSE;
    }

    while (next(parent, 0, &itemName, &isDir, &fileSize, &attr, &lastWriteTime, nextParam, &enumError) != NULL)
    {
        if (!itemName || !*itemName) continue;
        if (strcmp(itemName, ".") == 0 || strcmp(itemName, "..") == 0) continue;

        if (progressDlg->IsCancelled())
        {
            break;
        }

        std::wstring wName = SharePointHttp::HttpClient::AnsiToWide(itemName);
        ResultNode node;
        UploadTree(ctx, wSourcePath + wName, SharePointHttp::HttpClient::WideToUtf8(wName), isDir != 0,
                   folderItemId, siblings, node);

        if (!copy) CleanupLocalMoved(node);
        if (ctx.cancelled) break;
    }

    progressDlg->Close();

    if (ctx.cancelled || progressDlg->IsCancelled())
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

    // Collect the selection (files and folders) first, so totals are exact.
    struct TopEntry
    {
        DlNode tree;
        std::string name;
    };
    std::vector<TopEntry> entries;
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
        if (f->Name && strcmp(f->Name, "..") != 0)
        {
            SharePointApi::SharePointItem item;
            const auto* pItem = FindItemObjectByPanelName(f->Name, isDir);
            if (pItem)
            {
                item = *pItem;
                if (item.driveId.empty()) item.driveId = driveId;
            }
            else
            {
                item.id = FindItemByPanelName(f->Name, isDir);
                item.driveId = driveId;
                item.name = f->Name;
                item.size = (int64_t)f->Size.Value;
            }
            item.isFolder = (isDir != FALSE);

            entries.emplace_back();
            entries.back().name = f->Name;
            std::string treeErr;
            if (!BuildDlTree(item, entries.back().tree, totalCount, totalBytes, treeErr, 0))
            {
                ShowTransferError(parent, FormatFolderError(f->Name, treeErr));
                cancelOrHandlePath = TRUE;
                return TRUE;
            }
        }
        if (focused) break;
    }

    if (entries.empty())
    {
        return TRUE;
    }

    // A single explicit target file name applies only to exactly one selected file.
    std::wstring wTargetDir, wSingleTargetFileName;
    bool singleFile = (entries.size() == 1 && !entries[0].tree.item.isFolder);
    ParseLocalTarget(targetPath, singleFile ? 1 : 2, singleFile ? entries[0].name : std::string(),
                     wTargetDir, wSingleTargetFileName);
    EnsureLocalDirectoryExists(wTargetDir);

    auto progressDlg = CDialogTransferProgress::Create(parent, true);
    progressDlg->SetTotalItemsCount(totalCount);
    progressDlg->SetTotalBatchBytes(totalBytes);

    DownloadCtx ctx;
    ctx.parent = parent;
    ctx.dlg = progressDlg;
    ctx.totalBytes = totalBytes;

    for (auto& e : entries)
    {
        if (progressDlg->IsCancelled()) break;

        std::wstring destPath = wTargetDir;
        if (!destPath.empty() && destPath.back() != L'\\') destPath += L"\\";
        if (singleFile && !wSingleTargetFileName.empty())
        {
            destPath += wSingleTargetFileName;
        }
        else
        {
            std::wstring comp;
            if (!SharePointTransfer::MakeSafeLocalComponent(SharePointHttp::HttpClient::Utf8ToWide(e.name), comp))
            {
                char buf[2 * MAX_PATH + 256];
                std::string fmt = LoadStr(IDS_XFER_ERR_UNSAFE_NAME_FMT);
                snprintf(buf, sizeof(buf), fmt.c_str(), e.name.c_str());
                ShowTransferError(parent, buf);
                continue; // skipped: never deleted remotely
            }
            destPath += comp;
        }

        ResultNode result;
        DownloadTree(ctx, e.tree, destPath, result);

        if (!copy) CleanupRemoteMoved(driveId, result);
        if (ctx.cancelled || ctx.aborted) break;
    }

    if (ctx.aborted)
    {
        cancelOrHandlePath = TRUE;
        SharePointCache::CacheManager::GetInstance().InvalidateAll();
        return TRUE;
    }

    progressDlg->Close();

    if (ctx.cancelled || progressDlg->IsCancelled())
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

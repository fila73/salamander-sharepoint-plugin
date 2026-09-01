// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <windows.h>
#include <atomic>
#include "sharepoint_http.h"

namespace SharePointApi
{

struct SharePointItem
{
    std::string id;
    std::string name;
    int64_t size = 0;
    FILETIME modifiedTime = {0, 0};
    FILETIME createdTime = {0, 0};
    bool isFolder = false;
    bool isSite = false;
    bool isDrive = false; // Document Library
    bool isOneDrive = false;
    bool isTrashed = false;
    bool isShared = false;
    std::string authorName;
    std::string authorEmail;
    std::string lastModifyingUserName;
    std::string lastModifyingUserEmail;
    bool canEdit = true;
    bool canDelete = true;
    bool canRename = true;
    bool canDownload = true;
    std::string parentId;
    std::string parentPath;
    std::string webUrl;
    std::string downloadUrl;
    std::string driveId;
    std::string siteId;
};

struct SearchOptions
{
    std::string queryNamed;       // Name / wildcard to search, e.g. "report", "*.xlsx"
    bool searchSubdirs = true;
    bool searchContent = false;
    std::string queryContent;
    bool caseSensitive = false;
    std::string siteId;           // Optional scope site ID
    std::string driveId;          // Optional scope drive ID
    std::string folderScopeId;    // Scope folder ID
    std::string targetFolderPath; // Normalized target path e.g. "\Sites\Marketing\Documents\Q1"
    int typeFilter = 0;           // 0: All, 1: Word, 2: Excel, 3: PowerPoint, 4: PDF, 5: Images, 6: Folders
};

struct StorageQuota
{
    int64_t usage = 0;
    int64_t limit = 0;
    int64_t remaining = 0;
    int64_t deleted = 0;
};

class ApiClient
{
public:
    static ApiClient& GetInstance();

    bool SearchFiles(const SearchOptions& opts,
                     std::vector<SharePointItem>& resultsOut,
                     const std::atomic<bool>* cancelFlag = nullptr,
                     std::string* errorOut = nullptr);

    bool GetStorageQuota(StorageQuota& quota, std::string* errorOut = nullptr);

    bool ListSites(std::vector<SharePointItem>& sitesOut, std::string* errorOut = nullptr);
    bool ListSiteDrives(const std::string& siteId, std::vector<SharePointItem>& drivesOut, std::string* errorOut = nullptr);
    bool ListDriveChildren(const std::string& driveId,
                           const std::string& folderItemId,
                           std::vector<SharePointItem>& itemsOut,
                           std::string* errorOut = nullptr);

    bool ListOneDriveRoot(std::vector<SharePointItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListSharedWithMe(std::vector<SharePointItem>& itemsOut, std::string* errorOut = nullptr);
    bool ListRecent(std::vector<SharePointItem>& itemsOut, std::string* errorOut = nullptr);

    bool GetDriveDelta(const std::string& driveId,
                       const std::string& deltaTokenOrUrl,
                       std::vector<SharePointItem>& changedItemsOut,
                       std::string& newDeltaUrlOut,
                       std::string* errorOut = nullptr);

    bool GetItemMetadata(const std::string& driveId,
                         const std::string& itemId,
                         SharePointItem& itemOut,
                         std::string* errorOut = nullptr);

    bool CreateFolder(const std::string& driveId,
                      const std::string& parentItemId,
                      const std::string& folderName,
                      SharePointItem& itemOut,
                      std::string* errorOut = nullptr);

    bool RenameItem(const std::string& driveId,
                    const std::string& itemId,
                    const std::string& newName,
                    std::string* errorOut = nullptr);

    bool DeleteItem(const std::string& driveId,
                    const std::string& itemId,
                    std::string* errorOut = nullptr);

    bool DownloadFile(const SharePointItem& item,
                      const std::wstring& targetLocalPath,
                      SharePointHttp::ProgressCallback progressCallback = nullptr,
                      const bool* cancelFlag = nullptr,
                      std::string* errorOut = nullptr);

    bool UploadFile(const std::string& driveId,
                    const std::string& parentFolderId,
                    const std::wstring& localFilePath,
                    const std::string& destFileName,
                    SharePointHttp::ProgressCallback progressCallback = nullptr,
                    const bool* cancelFlag = nullptr,
                    SharePointItem* itemOut = nullptr,
                    std::string* errorOut = nullptr);

    static FILETIME ParseIso8601ToFt(const std::string& iso);

private:
    ApiClient() = default;
    ~ApiClient() = default;

    SharePointItem ParseDriveItemJson(const class SharePointJson::Value& v,
                                      const std::string& defaultDriveId = "",
                                      const std::string& defaultSiteId = "");
};

} // namespace SharePointApi

// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "sharepoint_api.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <functional>
#include <windows.h>

namespace SharePointCache
{

struct CachedFolder
{
    std::string folderKey;
    std::vector<SharePointApi::SharePointItem> items;
    uint64_t lastFetchedTick = 0;
    bool isValid = false;
};

class CacheManager
{
public:
    static CacheManager& GetInstance();

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetCheckIntervalMs(DWORD ms) { m_checkIntervalMs = ms; }
    DWORD GetCheckIntervalMs() const { return m_checkIntervalMs; }

    void SetSmartCtrlR(bool smart) { m_smartCtrlR = smart; }
    bool IsSmartCtrlR() const { return m_smartCtrlR; }

    void SetCurrentAccount(const std::string& email);
    const std::string& GetCurrentAccount() const { return m_currentAccountEmail; }

    void SwitchAccount(const std::string& newAccountEmail);
    bool LoadFromDisk();
    bool SaveToDisk();
    void ClearDiskCache(const std::string& accountEmail = "");

    bool GetFolder(const std::string& folderKey, std::vector<SharePointApi::SharePointItem>& itemsOut);
    void PutFolder(const std::string& folderKey, const std::vector<SharePointApi::SharePointItem>& items);

    void InvalidateFolder(const std::string& folderKey);
    void InvalidateFolderIds(const std::vector<std::string>& folderIds);
    void InvalidateAll();
    void InvalidateVirtualFolders();

    void SetFolderSize(const std::string& folderId, int64_t size);
    bool GetFolderSize(const std::string& folderId, int64_t& sizeOut);
    bool ComputeFolderSizeFromCache(const std::string& folderId,
                                   int64_t& sizeOut,
                                   int& filesCountOut,
                                   int& dirsCountOut,
                                   std::set<std::string>* pVisitedFolderIdsOut = nullptr);

    bool CheckForRemoteChanges(bool forceCheck = false, std::vector<std::string>* pChangedFoldersOut = nullptr);
    void CheckForRemoteChangesAsync(std::function<void(const std::vector<std::string>& changedFolders)> onComplete = nullptr,
                                    bool forceCheck = false);

    void AddOrUpdateItem(const std::string& folderKey, const SharePointApi::SharePointItem& item);
    void RemoveItem(const std::string& folderKey, const std::string& itemId);
    void RenameItem(const std::string& folderKey, const std::string& itemId, const std::string& newName);

private:
    CacheManager();
    ~CacheManager();

    std::wstring GetCacheFilePath(const std::string& accountEmail) const;

    std::string m_currentAccountEmail;
    std::map<std::string, CachedFolder> m_folders;
    std::map<std::string, int64_t> m_folderSizes;
    std::map<std::string, std::string> m_driveDeltaUrls;
    uint64_t m_lastChangeCheckTick = 0;
    DWORD m_checkIntervalMs = 30000;
    bool m_smartCtrlR = false;
    bool m_enabled = true;
    bool m_dirty = false;
    std::atomic<bool> m_checkingChanges{false};
    std::mutex m_mutex;
};

} // namespace SharePointCache

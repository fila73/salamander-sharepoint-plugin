// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sharepoint_cache.h"
#include "sharepoint_auth.h"
#include "sharepoint_log.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <shlobj.h>

namespace SharePointCache
{

static uint64_t GetTickMs()
{
    return (uint64_t)GetTickCount64();
}

CacheManager& CacheManager::GetInstance()
{
    static CacheManager instance;
    return instance;
}

CacheManager::CacheManager()
{
    m_currentAccountEmail = SharePointAuth::AuthManager::GetInstance().GetTokens().accountEmail;
    LoadFromDisk();
}

CacheManager::~CacheManager()
{
    SaveToDisk();
}

std::wstring CacheManager::GetCacheFilePath(const std::string& accountEmail) const
{
    wchar_t appData[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)))
    {
        return L"";
    }

    std::wstring dir = std::wstring(appData) + L"\\Open Salamander\\plugins\\sharepoint";
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander").c_str(), NULL);
    CreateDirectoryW((std::wstring(appData) + L"\\Open Salamander\\plugins").c_str(), NULL);
    CreateDirectoryW(dir.c_str(), NULL);

    std::string safeEmail = accountEmail.empty() ? "default" : accountEmail;
    std::replace(safeEmail.begin(), safeEmail.end(), '@', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '.', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), ':', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '/', '_');
    std::replace(safeEmail.begin(), safeEmail.end(), '\\', '_');

    return dir + L"\\cache_" + SharePointHttp::HttpClient::Utf8ToWide(safeEmail) + L".bin";
}

void CacheManager::SetCurrentAccount(const std::string& email)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentAccountEmail != email)
    {
        if (m_dirty) SaveToDisk();
        m_currentAccountEmail = email;
        m_folders.clear();
        m_folderSizes.clear();
        m_driveDeltaUrls.clear();
        LoadFromDisk();
    }
}

void CacheManager::SwitchAccount(const std::string& newAccountEmail)
{
    SetCurrentAccount(newAccountEmail);
}

void CacheManager::ClearDiskCache(const std::string& accountEmail)
{
    std::string email = accountEmail.empty() ? m_currentAccountEmail : accountEmail;
    std::wstring path = GetCacheFilePath(email);
    if (!path.empty())
    {
        DeleteFileW(path.c_str());
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (email == m_currentAccountEmail)
    {
        m_folders.clear();
        m_folderSizes.clear();
        m_driveDeltaUrls.clear();
        m_dirty = false;
    }
}

bool CacheManager::GetFolder(const std::string& folderKey, std::vector<SharePointApi::SharePointItem>& itemsOut)
{
    if (!m_enabled) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end() && it->second.isValid)
    {
        itemsOut = it->second.items;
        return true;
    }
    return false;
}

void CacheManager::PutFolder(const std::string& folderKey, const std::vector<SharePointApi::SharePointItem>& items)
{
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    CachedFolder cf;
    cf.folderKey = folderKey;
    cf.items = items;
    cf.lastFetchedTick = GetTickMs();
    cf.isValid = true;
    m_folders[folderKey] = cf;
    m_dirty = true;
}

void CacheManager::InvalidateFolder(const std::string& folderKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        it->second.isValid = false;
        m_dirty = true;
    }
}

void CacheManager::InvalidateFolderIds(const std::vector<std::string>& folderIds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& fid : folderIds)
    {
        for (auto& [k, folder] : m_folders)
        {
            if (k.find(fid) != std::string::npos)
            {
                folder.isValid = false;
                m_dirty = true;
            }
        }
    }
}

void CacheManager::InvalidateAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [k, folder] : m_folders)
    {
        folder.isValid = false;
    }
    m_dirty = true;
}

void CacheManager::InvalidateVirtualFolders()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [k, folder] : m_folders)
    {
        if (k == "sharedWithMe" || k == "recent" || k == "root" || k == "sites")
        {
            folder.isValid = false;
        }
    }
    m_dirty = true;
}

void CacheManager::SetFolderSize(const std::string& folderId, int64_t size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_folderSizes[folderId] = size;
    m_dirty = true;
}

bool CacheManager::GetFolderSize(const std::string& folderId, int64_t& sizeOut)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folderSizes.find(folderId);
    if (it != m_folderSizes.end())
    {
        sizeOut = it->second;
        return true;
    }
    return false;
}

bool CacheManager::ComputeFolderSizeFromCache(const std::string& folderId,
                                             int64_t& sizeOut,
                                             int& filesCountOut,
                                             int& dirsCountOut,
                                             std::set<std::string>* pVisitedFolderIdsOut)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<std::string> localVisited;
    std::set<std::string>& visited = pVisitedFolderIdsOut ? *pVisitedFolderIdsOut : localVisited;

    if (visited.find(folderId) != visited.end()) return true;
    visited.insert(folderId);

    bool allCached = true;
    sizeOut = 0;
    filesCountOut = 0;
    dirsCountOut = 0;

    for (const auto& [k, folder] : m_folders)
    {
        if (k.find(folderId) != std::string::npos && folder.isValid)
        {
            for (const auto& item : folder.items)
            {
                if (item.isFolder)
                {
                    dirsCountOut++;
                    int64_t subSize = 0;
                    int subFiles = 0, subDirs = 0;
                    if (ComputeFolderSizeFromCache(item.id, subSize, subFiles, subDirs, &visited))
                    {
                        sizeOut += subSize;
                        filesCountOut += subFiles;
                        dirsCountOut += subDirs;
                    }
                    else
                    {
                        allCached = false;
                    }
                }
                else
                {
                    filesCountOut++;
                    sizeOut += item.size;
                }
            }
            return allCached;
        }
    }

    return false;
}

void CacheManager::AddOrUpdateItem(const std::string& folderKey, const SharePointApi::SharePointItem& item)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        bool found = false;
        for (auto& existing : it->second.items)
        {
            if (existing.id == item.id)
            {
                existing = item;
                found = true;
                break;
            }
        }
        if (!found)
        {
            it->second.items.push_back(item);
        }
        m_dirty = true;
    }
}

void CacheManager::RemoveItem(const std::string& folderKey, const std::string& itemId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        auto& items = it->second.items;
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&itemId](const SharePointApi::SharePointItem& item) {
                                       return item.id == itemId;
                                   }),
                    items.end());
        m_dirty = true;
    }
}

void CacheManager::RenameItem(const std::string& folderKey, const std::string& itemId, const std::string& newName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_folders.find(folderKey);
    if (it != m_folders.end())
    {
        for (auto& item : it->second.items)
        {
            if (item.id == itemId)
            {
                item.name = newName;
                m_dirty = true;
                break;
            }
        }
    }
}

bool CacheManager::CheckForRemoteChanges(bool forceCheck, std::vector<std::string>* pChangedFoldersOut)
{
    if (!m_enabled) return true;

    uint64_t now = GetTickMs();
    if (!forceCheck && (now - m_lastChangeCheckTick < m_checkIntervalMs))
    {
        return true;
    }

    if (m_checkingChanges.exchange(true))
    {
        return true;
    }

    m_lastChangeCheckTick = now;

    std::vector<std::string> activeDriveIds;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [k, folder] : m_folders)
        {
            for (const auto& item : folder.items)
            {
                if (!item.driveId.empty() && std::find(activeDriveIds.begin(), activeDriveIds.end(), item.driveId) == activeDriveIds.end())
                {
                    activeDriveIds.push_back(item.driveId);
                }
            }
        }
    }

    if (activeDriveIds.empty()) activeDriveIds.push_back("me");

    std::vector<std::string> changedFolderIds;
    for (const auto& driveId : activeDriveIds)
    {
        std::string deltaUrl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_driveDeltaUrls.find(driveId);
            if (it != m_driveDeltaUrls.end()) deltaUrl = it->second;
        }

        std::vector<SharePointApi::SharePointItem> changedItems;
        std::string newDeltaUrl;
        std::string err;

        if (SharePointApi::ApiClient::GetInstance().GetDriveDelta(driveId, deltaUrl, changedItems, newDeltaUrl, &err))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_driveDeltaUrls[driveId] = newDeltaUrl;

            for (const auto& item : changedItems)
            {
                if (!item.parentId.empty())
                {
                    changedFolderIds.push_back(item.parentId);
                }
                changedFolderIds.push_back(item.id);
            }
        }
    }

    if (!changedFolderIds.empty())
    {
        InvalidateFolderIds(changedFolderIds);
        InvalidateVirtualFolders();
        if (pChangedFoldersOut)
        {
            *pChangedFoldersOut = changedFolderIds;
        }
    }

    m_checkingChanges = false;
    return true;
}

void CacheManager::CheckForRemoteChangesAsync(std::function<void(const std::vector<std::string>& changedFolders)> onComplete,
                                              bool forceCheck)
{
    std::thread([this, onComplete, forceCheck]() {
        std::vector<std::string> changed;
        if (CheckForRemoteChanges(forceCheck, &changed))
        {
            if (onComplete && !changed.empty())
            {
                onComplete(changed);
            }
        }
    }).detach();
}

static void WriteString(std::ofstream& ofs, const std::string& str)
{
    uint32_t len = (uint32_t)str.length();
    ofs.write((const char*)&len, sizeof(len));
    if (len > 0)
    {
        ofs.write(str.data(), len);
    }
}

static bool ReadString(std::ifstream& ifs, std::string& str)
{
    uint32_t len = 0;
    if (!ifs.read((char*)&len, sizeof(len))) return false;
    str.resize(len);
    if (len > 0)
    {
        if (!ifs.read(&str[0], len)) return false;
    }
    return true;
}

bool CacheManager::SaveToDisk()
{
    std::wstring path = GetCacheFilePath(m_currentAccountEmail);
    if (path.empty()) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream ofs(path.c_str(), std::ios::binary);
    if (!ofs.is_open()) return false;

    uint32_t magic = 0x53504331; // "SPC1"
    ofs.write((const char*)&magic, sizeof(magic));

    uint32_t folderCount = (uint32_t)m_folders.size();
    ofs.write((const char*)&folderCount, sizeof(folderCount));

    for (const auto& [k, folder] : m_folders)
    {
        WriteString(ofs, folder.folderKey);
        ofs.write((const char*)&folder.lastFetchedTick, sizeof(folder.lastFetchedTick));
        uint8_t valid = folder.isValid ? 1 : 0;
        ofs.write((const char*)&valid, sizeof(valid));

        uint32_t itemCount = (uint32_t)folder.items.size();
        ofs.write((const char*)&itemCount, sizeof(itemCount));

        for (const auto& item : folder.items)
        {
            WriteString(ofs, item.id);
            WriteString(ofs, item.name);
            ofs.write((const char*)&item.size, sizeof(item.size));
            ofs.write((const char*)&item.createdTime, sizeof(item.createdTime));
            ofs.write((const char*)&item.modifiedTime, sizeof(item.modifiedTime));

            uint8_t flags = 0;
            if (item.isFolder) flags |= 0x01;
            if (item.isSite) flags |= 0x02;
            if (item.isDrive) flags |= 0x04;
            if (item.isOneDrive) flags |= 0x08;
            if (item.isTrashed) flags |= 0x10;
            if (item.isShared) flags |= 0x20;
            ofs.write((const char*)&flags, sizeof(flags));

            WriteString(ofs, item.authorName);
            WriteString(ofs, item.authorEmail);
            WriteString(ofs, item.lastModifyingUserName);
            WriteString(ofs, item.lastModifyingUserEmail);
            WriteString(ofs, item.parentId);
            WriteString(ofs, item.parentPath);
            WriteString(ofs, item.webUrl);
            WriteString(ofs, item.downloadUrl);
            WriteString(ofs, item.driveId);
            WriteString(ofs, item.siteId);
        }
    }

    uint32_t driveCount = (uint32_t)m_driveDeltaUrls.size();
    ofs.write((const char*)&driveCount, sizeof(driveCount));
    for (const auto& [driveId, deltaUrl] : m_driveDeltaUrls)
    {
        WriteString(ofs, driveId);
        WriteString(ofs, deltaUrl);
    }

    m_dirty = false;
    return true;
}

bool CacheManager::LoadFromDisk()
{
    std::wstring path = GetCacheFilePath(m_currentAccountEmail);
    if (path.empty()) return false;

    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs.is_open()) return false;

    uint32_t magic = 0;
    if (!ifs.read((char*)&magic, sizeof(magic)) || magic != 0x53504331) return false;

    uint32_t folderCount = 0;
    if (!ifs.read((char*)&folderCount, sizeof(folderCount))) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_folders.clear();

    for (uint32_t f = 0; f < folderCount; ++f)
    {
        CachedFolder folder;
        if (!ReadString(ifs, folder.folderKey)) return false;
        if (!ifs.read((char*)&folder.lastFetchedTick, sizeof(folder.lastFetchedTick))) return false;
        uint8_t valid = 0;
        if (!ifs.read((char*)&valid, sizeof(valid))) return false;
        folder.isValid = (valid != 0);

        uint32_t itemCount = 0;
        if (!ifs.read((char*)&itemCount, sizeof(itemCount))) return false;

        for (uint32_t i = 0; i < itemCount; ++i)
        {
            SharePointApi::SharePointItem item;
            if (!ReadString(ifs, item.id)) return false;
            if (!ReadString(ifs, item.name)) return false;
            if (!ifs.read((char*)&item.size, sizeof(item.size))) return false;
            if (!ifs.read((char*)&item.createdTime, sizeof(item.createdTime))) return false;
            if (!ifs.read((char*)&item.modifiedTime, sizeof(item.modifiedTime))) return false;

            uint8_t flags = 0;
            if (!ifs.read((char*)&flags, sizeof(flags))) return false;
            item.isFolder = (flags & 0x01) != 0;
            item.isSite = (flags & 0x02) != 0;
            item.isDrive = (flags & 0x04) != 0;
            item.isOneDrive = (flags & 0x08) != 0;
            item.isTrashed = (flags & 0x10) != 0;
            item.isShared = (flags & 0x20) != 0;

            if (!ReadString(ifs, item.authorName)) return false;
            if (!ReadString(ifs, item.authorEmail)) return false;
            if (!ReadString(ifs, item.lastModifyingUserName)) return false;
            if (!ReadString(ifs, item.lastModifyingUserEmail)) return false;
            if (!ReadString(ifs, item.parentId)) return false;
            if (!ReadString(ifs, item.parentPath)) return false;
            if (!ReadString(ifs, item.webUrl)) return false;
            if (!ReadString(ifs, item.downloadUrl)) return false;
            if (!ReadString(ifs, item.driveId)) return false;
            if (!ReadString(ifs, item.siteId)) return false;

            folder.items.push_back(item);
        }

        m_folders[folder.folderKey] = folder;
    }

    uint32_t driveCount = 0;
    if (ifs.read((char*)&driveCount, sizeof(driveCount)))
    {
        for (uint32_t d = 0; d < driveCount; ++d)
        {
            std::string driveId, deltaUrl;
            if (ReadString(ifs, driveId) && ReadString(ifs, deltaUrl))
            {
                m_driveDeltaUrls[driveId] = deltaUrl;
            }
        }
    }

    m_dirty = false;
    return true;
}

} // namespace SharePointCache

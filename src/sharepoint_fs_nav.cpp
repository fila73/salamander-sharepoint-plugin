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

static std::map<std::string, std::string> s_pathToIdMap;
static std::map<std::string, std::string> s_pathToDriveMap;
static std::map<std::string, std::string> s_pathToSiteMap;
static std::mutex s_pathMapMutex;

// Transfer variables for custom columns
static const CFileData** s_transferFileData = NULL;
static int* s_transferIsDir = NULL;
static char* s_transferBuffer = NULL;
static int* s_transferLen = NULL;
static DWORD* s_transferRowData = NULL;
static CPluginDataInterfaceAbstract** s_transferPluginDataIface = NULL;
static DWORD* s_transferActCustomData = NULL;

static DWORD s_authorWidth = MAKELONG(140, 140);
static DWORD s_authorFixedWidth = MAKELONG(0, 0);
static DWORD s_modifiedByWidth = MAKELONG(140, 140);
static DWORD s_modifiedByFixedWidth = MAKELONG(0, 0);
static DWORD s_sharedWidth = MAKELONG(60, 60);
static DWORD s_sharedFixedWidth = MAKELONG(0, 0);
static DWORD s_webUrlWidth = MAKELONG(200, 200);
static DWORD s_webUrlFixedWidth = MAKELONG(0, 0);

#define SHAREPOINT_COL_AUTHOR_ID 1
#define SHAREPOINT_COL_MODIFIED_BY_ID 2
#define SHAREPOINT_COL_SHARED_ID 3
#define SHAREPOINT_COL_WEBURL_ID 4

struct SharePointPluginData
{
    std::string id;
    std::string driveId;
    std::string siteId;
    std::string authorName;
    std::string lastModifyingUserName;
    std::string webUrl;
    bool isShared = false;
};

static CSharePointPluginDataInterface s_pluginDataInterface;

void WINAPI CSharePointPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    if (file.PluginData != 0)
    {
        delete reinterpret_cast<SharePointPluginData*>(file.PluginData);
        file.PluginData = 0;
    }
}

int WINAPI CSharePointPluginDataInterface::CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
{
    if (!file1 || !file2) return 0;
    return _stricmp(file1->Name ? file1->Name : "", file2->Name ? file2->Name : "");
}

static void WINAPI GetAuthorColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const SharePointPluginData*>((*s_transferFileData)->PluginData);
        if (!pData->authorName.empty())
        {
            int len = (int)pData->authorName.length();
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, pData->authorName.data(), len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

static void WINAPI GetModifiedByColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const SharePointPluginData*>((*s_transferFileData)->PluginData);
        if (!pData->lastModifyingUserName.empty())
        {
            int len = (int)pData->lastModifyingUserName.length();
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, pData->lastModifyingUserName.data(), len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

static void WINAPI GetSharedColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const SharePointPluginData*>((*s_transferFileData)->PluginData);
        const char* str = pData->isShared ? "Yes" : "No";
        int len = (int)strlen(str);
        if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
        memcpy(s_transferBuffer, str, len);
        *s_transferLen = len;
        return;
    }
    *s_transferLen = 0;
}

static void WINAPI GetWebUrlColumnText()
{
    if (s_transferFileData && *s_transferFileData && (*s_transferFileData)->PluginData)
    {
        auto* pData = reinterpret_cast<const SharePointPluginData*>((*s_transferFileData)->PluginData);
        if (!pData->webUrl.empty())
        {
            int len = (int)pData->webUrl.length();
            if (len > TRANSFER_BUFFER_MAX - 1) len = TRANSFER_BUFFER_MAX - 1;
            memcpy(s_transferBuffer, pData->webUrl.data(), len);
            *s_transferLen = len;
            return;
        }
    }
    *s_transferLen = 0;
}

void WINAPI CSharePointPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                                      const char* archivePath, const CFileData* upperDir)
{
    if (!view) return;

    view->GetTransferVariables(s_transferFileData, s_transferIsDir, s_transferBuffer, s_transferLen,
                               s_transferRowData, s_transferPluginDataIface, s_transferActCustomData);

    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        int count = view->GetColumnsCount();

        // Author column
        CColumn colAuthor;
        memset(&colAuthor, 0, sizeof(colAuthor));
        lstrcpynA(colAuthor.Name, LoadStr(IDS_COL_AUTHOR), sizeof(colAuthor.Name));
        lstrcpynA(colAuthor.Description, LoadStr(IDS_COL_AUTHOR_DESC), sizeof(colAuthor.Description));
        colAuthor.GetText = GetAuthorColumnText;
        colAuthor.CustomData = SHAREPOINT_COL_AUTHOR_ID;
        colAuthor.SupportSorting = 1;
        colAuthor.LeftAlignment = 1;
        colAuthor.ID = COLUMN_ID_CUSTOM;
        colAuthor.Width = leftPanel ? LOWORD(s_authorWidth) : HIWORD(s_authorWidth);
        colAuthor.FixedWidth = leftPanel ? LOWORD(s_authorFixedWidth) : HIWORD(s_authorFixedWidth);
        view->InsertColumn(count++, &colAuthor);

        // Modified By column
        CColumn colModified;
        memset(&colModified, 0, sizeof(colModified));
        lstrcpynA(colModified.Name, LoadStr(IDS_COL_MODIFIED_BY), sizeof(colModified.Name));
        lstrcpynA(colModified.Description, LoadStr(IDS_COL_MODIFIED_BY_DESC), sizeof(colModified.Description));
        colModified.GetText = GetModifiedByColumnText;
        colModified.CustomData = SHAREPOINT_COL_MODIFIED_BY_ID;
        colModified.SupportSorting = 1;
        colModified.LeftAlignment = 1;
        colModified.ID = COLUMN_ID_CUSTOM;
        colModified.Width = leftPanel ? LOWORD(s_modifiedByWidth) : HIWORD(s_modifiedByWidth);
        colModified.FixedWidth = leftPanel ? LOWORD(s_modifiedByFixedWidth) : HIWORD(s_modifiedByFixedWidth);
        view->InsertColumn(count++, &colModified);

        // Shared column
        CColumn colShared;
        memset(&colShared, 0, sizeof(colShared));
        lstrcpynA(colShared.Name, LoadStr(IDS_COL_SHARED), sizeof(colShared.Name));
        lstrcpynA(colShared.Description, LoadStr(IDS_COL_SHARED_DESC), sizeof(colShared.Description));
        colShared.GetText = GetSharedColumnText;
        colShared.CustomData = SHAREPOINT_COL_SHARED_ID;
        colShared.SupportSorting = 1;
        colShared.LeftAlignment = 1;
        colShared.ID = COLUMN_ID_CUSTOM;
        colShared.Width = leftPanel ? LOWORD(s_sharedWidth) : HIWORD(s_sharedWidth);
        colShared.FixedWidth = leftPanel ? LOWORD(s_sharedFixedWidth) : HIWORD(s_sharedFixedWidth);
        view->InsertColumn(count++, &colShared);

        // Web URL column
        CColumn colWebUrl;
        memset(&colWebUrl, 0, sizeof(colWebUrl));
        lstrcpynA(colWebUrl.Name, LoadStr(IDS_COL_WEBURL), sizeof(colWebUrl.Name));
        lstrcpynA(colWebUrl.Description, LoadStr(IDS_COL_WEBURL_DESC), sizeof(colWebUrl.Description));
        colWebUrl.GetText = GetWebUrlColumnText;
        colWebUrl.CustomData = SHAREPOINT_COL_WEBURL_ID;
        colWebUrl.SupportSorting = 1;
        colWebUrl.LeftAlignment = 1;
        colWebUrl.ID = COLUMN_ID_CUSTOM;
        colWebUrl.Width = leftPanel ? LOWORD(s_webUrlWidth) : HIWORD(s_webUrlWidth);
        colWebUrl.FixedWidth = leftPanel ? LOWORD(s_webUrlFixedWidth) : HIWORD(s_webUrlFixedWidth);
        view->InsertColumn(count++, &colWebUrl);
    }
}

void WINAPI CSharePointPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (!column) return;
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case SHAREPOINT_COL_AUTHOR_ID: s_authorFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_authorFixedWidth)); break;
        case SHAREPOINT_COL_MODIFIED_BY_ID: s_modifiedByFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_modifiedByFixedWidth)); break;
        case SHAREPOINT_COL_SHARED_ID: s_sharedFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_sharedFixedWidth)); break;
        case SHAREPOINT_COL_WEBURL_ID: s_webUrlFixedWidth = MAKELONG(newFixedWidth, HIWORD(s_webUrlFixedWidth)); break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case SHAREPOINT_COL_AUTHOR_ID: s_authorFixedWidth = MAKELONG(LOWORD(s_authorFixedWidth), newFixedWidth); break;
        case SHAREPOINT_COL_MODIFIED_BY_ID: s_modifiedByFixedWidth = MAKELONG(LOWORD(s_modifiedByFixedWidth), newFixedWidth); break;
        case SHAREPOINT_COL_SHARED_ID: s_sharedFixedWidth = MAKELONG(LOWORD(s_sharedFixedWidth), newFixedWidth); break;
        case SHAREPOINT_COL_WEBURL_ID: s_webUrlFixedWidth = MAKELONG(LOWORD(s_webUrlFixedWidth), newFixedWidth); break;
        }
    }
}

void WINAPI CSharePointPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (!column) return;
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case SHAREPOINT_COL_AUTHOR_ID: s_authorWidth = MAKELONG(newWidth, HIWORD(s_authorWidth)); break;
        case SHAREPOINT_COL_MODIFIED_BY_ID: s_modifiedByWidth = MAKELONG(newWidth, HIWORD(s_modifiedByWidth)); break;
        case SHAREPOINT_COL_SHARED_ID: s_sharedWidth = MAKELONG(newWidth, HIWORD(s_sharedWidth)); break;
        case SHAREPOINT_COL_WEBURL_ID: s_webUrlWidth = MAKELONG(newWidth, HIWORD(s_webUrlWidth)); break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case SHAREPOINT_COL_AUTHOR_ID: s_authorWidth = MAKELONG(LOWORD(s_authorWidth), newWidth); break;
        case SHAREPOINT_COL_MODIFIED_BY_ID: s_modifiedByWidth = MAKELONG(LOWORD(s_modifiedByWidth), newWidth); break;
        case SHAREPOINT_COL_SHARED_ID: s_sharedWidth = MAKELONG(LOWORD(s_sharedWidth), newWidth); break;
        case SHAREPOINT_COL_WEBURL_ID: s_webUrlWidth = MAKELONG(LOWORD(s_webUrlWidth), newWidth); break;
        }
    }
}

static void AddItemToDir(CSalamanderDirectoryAbstract* dir, const char* name,
                         bool isDir, int64_t size, bool sizeValid, const FILETIME* ft,
                         CPluginDataInterfaceAbstract* pluginData,
                         const SharePointApi::SharePointItem* item = nullptr)
{
    if (!dir || !name) return;

    std::string ansiName;
    if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0)
    {
        ansiName = name;
    }
    else
    {
        ansiName = SharePointHttp::HttpClient::Utf8ToAnsi(name);
    }

    CFileData file{};
    file.Name = SalamanderGeneral->DupStr(ansiName.c_str());
    if (!file.Name) return;

    file.NameLen = (int)strlen(file.Name);

    int sortByExtDirsAsFiles = 0;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    if (!sortByExtDirsAsFiles && isDir)
        file.Ext = file.Name + file.NameLen;
    else
    {
        const char* pDot = strrchr(file.Name, '.');
        file.Ext = (pDot != NULL) ? const_cast<char*>(pDot + 1) : file.Name + file.NameLen;
    }

    file.Size.SetUI64((unsigned __int64)(size < 0 ? 0 : size));
    if (sizeValid)
    {
        file.SizeValid = 1;
    }
    if (ft) file.LastWrite = *ft;
    file.Attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    file.DosName = NULL;
    file.PluginData = 0;

    if (item)
    {
        SharePointPluginData* pd = new SharePointPluginData();
        pd->id = item->id;
        pd->driveId = item->driveId;
        pd->siteId = item->siteId;
        pd->authorName = item->authorName;
        pd->lastModifyingUserName = item->lastModifyingUserName;
        pd->webUrl = item->webUrl;
        pd->isShared = item->isShared;
        file.PluginData = reinterpret_cast<DWORD_PTR>(pd);
    }

    file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    if (isDir)
    {
        dir->AddDir(NULL, file, pluginData);
    }
    else
    {
        dir->AddFile(NULL, file, pluginData);
    }
}

BOOL WINAPI CPluginFS::GetCurrentPath(char* userPart)
{
    if (!userPart) return FALSE;
    strncpy(userPart, m_currentPath.c_str(), MAX_PATH - 1);
    userPart[MAX_PATH - 1] = '\0';
    return TRUE;
}

BOOL WINAPI CPluginFS::GetFullName(CFileData& file, int isDir, char* buf, int bufSize)
{
    if (!buf || bufSize <= 0) return FALSE;

    lstrcpynA(buf, m_currentPath.c_str(), bufSize);
    if (isDir == 2) // ".."
    {
        return SalamanderGeneral->CutDirectory(buf, NULL);
    }
    else
    {
        if (buf[0] == '\0') strcpy(buf, "\\");
        if (buf[strlen(buf) - 1] != '\\') strcat(buf, "\\");
        if (file.Name) strncat(buf, file.Name, bufSize - strlen(buf) - 1);
        return TRUE;
    }
}

BOOL WINAPI CPluginFS::GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success)
{
    if (!path || pathSize <= 0)
    {
        success = FALSE;
        return FALSE;
    }

    std::string inputPath = path;
    std::string resultPath;

    if (inputPath.rfind("sharepoint:", 0) == 0)
    {
        resultPath = inputPath;
    }
    else if (!inputPath.empty() && inputPath[0] == '\\')
    {
        resultPath = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":" + inputPath;
    }
    else
    {
        std::string cur = m_currentPath;
        if (cur.empty() || cur.back() != '\\') cur += "\\";
        resultPath = std::string(fsName && *fsName ? fsName : AssignedFSName) + ":" + cur + inputPath;
    }

    lstrcpynA(path, resultPath.c_str(), pathSize);
    success = TRUE;
    return TRUE;
}

BOOL WINAPI CPluginFS::GetRootPath(char* userPart)
{
    if (!userPart) return FALSE;
    strcpy(userPart, "\\");
    return TRUE;
}

BOOL WINAPI CPluginFS::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    if (currentFSNameIndex != fsNameIndex) return FALSE;
    return SalamanderGeneral->IsTheSamePath(m_currentPath.c_str(), userPart ? userPart : "\\");
}

BOOL WINAPI CPluginFS::IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    return currentFSNameIndex == fsNameIndex;
}

BOOL WINAPI CPluginFS::ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                  const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                  BOOL forceRefresh, int mode)
{
    std::string oldPath = m_currentPath;

    if (pathWasCut) *pathWasCut = FALSE;
    if (cutFileName) *cutFileName = '\0';
    if (fsName) lstrcpynA(fsName, AssignedFSName, MAX_PATH);

    std::string target = (userPart && *userPart) ? userPart : "\\";
    if (target.empty() || target[0] != '\\') target = "\\" + target;

    while (target.length() > 1 && target.back() == '\\') target.pop_back();

    m_currentPath = target;

    // If navigating UP from a subpath, populate cutFileName so Salamander focuses the exited folder
    if (cutFileName && !oldPath.empty() && oldPath.length() > target.length())
    {
        std::string prefix = (target == "\\") ? "\\" : (target + "\\");
        if (oldPath.rfind(prefix, 0) == 0)
        {
            std::string rel = oldPath.substr(prefix.length());
            size_t slashPos = rel.find('\\');
            if (slashPos != std::string::npos)
            {
                rel = rel.substr(0, slashPos);
            }
            if (!rel.empty())
            {
                std::string ansiRel = SharePointHttp::HttpClient::Utf8ToAnsi(rel);
                lstrcpynA(cutFileName, ansiRel.c_str(), MAX_PATH);
                if (pathWasCut) *pathWasCut = TRUE;
            }
        }
    }

    return TRUE;
}

bool CPluginFS::ResolveCurrentTarget(std::string& outDriveId, std::string& outFolderItemId, std::string& outSiteId)
{
    std::lock_guard<std::mutex> lock(s_pathMapMutex);
    outDriveId.clear();
    outFolderItemId.clear();
    outSiteId.clear();

    auto itD = s_pathToDriveMap.find(m_currentPath);
    if (itD != s_pathToDriveMap.end()) outDriveId = itD->second;

    auto itI = s_pathToIdMap.find(m_currentPath);
    if (itI != s_pathToIdMap.end()) outFolderItemId = itI->second;

    auto itS = s_pathToSiteMap.find(m_currentPath);
    if (itS != s_pathToSiteMap.end()) outSiteId = itS->second;

    return !outDriveId.empty() || !outFolderItemId.empty() || !outSiteId.empty();
}

std::string CPluginFS::FindItemByPanelName(const char* name, BOOL isDir)
{
    if (!name) return "";
    for (const auto& item : m_currentItems)
    {
        if (item.isFolder == (isDir != FALSE) && _stricmp(item.name.c_str(), name) == 0)
        {
            return item.id;
        }
    }
    return "";
}

BOOL WINAPI CPluginFS::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                       CPluginDataInterfaceAbstract*& pluginData,
                                       int& iconsType, BOOL forceRefresh)
{
    if (!dir) return FALSE;

    iconsType = pitFromRegistry;
    pluginData = &s_pluginDataInterface;
    m_currentItems.clear();

    dir->SetFlags(SALDIRFLAG_IGNOREDUPDIRS);
    dir->SetValidData(VALID_DATA_EXTENSION | VALID_DATA_SIZE | VALID_DATA_TYPE |
                      VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_ATTRIBUTES);

    // Add up-directory if not at root
    if (!m_currentPath.empty() && m_currentPath != "\\" && _stricmp(m_currentPath.c_str(), "\\") != 0)
    {
        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);
        AddItemToDir(dir, "..", true, 0, false, &ft, pluginData);
    }

    auto& cache = SharePointCache::CacheManager::GetInstance();
    auto& api = SharePointApi::ApiClient::GetInstance();

    std::string cur = m_currentPath;
    if (cur.empty() || cur == "\\")
    {
        FILETIME ft = {0, 0};
        GetSystemTimeAsFileTime(&ft);

        SharePointApi::SharePointItem oneDrive;
        oneDrive.id = "onedrive_root";
        oneDrive.name = kOneDriveDir;
        oneDrive.isFolder = true;
        oneDrive.isOneDrive = true;
        m_currentItems.push_back(oneDrive);
        AddItemToDir(dir, kOneDriveDir, true, 0, false, &ft, pluginData, &oneDrive);

        SharePointApi::SharePointItem sites;
        sites.id = "sites_root";
        sites.name = kSitesDir;
        sites.isFolder = true;
        sites.isSite = true;
        m_currentItems.push_back(sites);
        AddItemToDir(dir, kSitesDir, true, 0, false, &ft, pluginData, &sites);

        SharePointApi::SharePointItem sharedWithMe;
        sharedWithMe.id = "shared_with_me";
        sharedWithMe.name = kSharedWithMeDir;
        sharedWithMe.isFolder = true;
        sharedWithMe.isShared = true;
        m_currentItems.push_back(sharedWithMe);
        AddItemToDir(dir, kSharedWithMeDir, true, 0, false, &ft, pluginData, &sharedWithMe);

        SharePointApi::SharePointItem recent;
        recent.id = "recent";
        recent.name = kRecentDir;
        recent.isFolder = true;
        m_currentItems.push_back(recent);
        AddItemToDir(dir, kRecentDir, true, 0, false, &ft, pluginData, &recent);

        SharePointApi::SharePointItem trash;
        trash.id = "trash";
        trash.name = kTrashDir;
        trash.isFolder = true;
        trash.isTrashed = true;
        m_currentItems.push_back(trash);
        AddItemToDir(dir, kTrashDir, true, 0, false, &ft, pluginData, &trash);

        return TRUE;
    }
    else if (_stricmp(cur.c_str(), "\\Sites") == 0)
    {
        std::vector<SharePointApi::SharePointItem> sites;
        if (!forceRefresh && cache.GetFolder("sites", sites))
        {
            m_currentItems = sites;
        }
        else
        {
            std::string err;
            if (api.ListSites(sites, &err))
            {
                m_currentItems = sites;
                cache.PutFolder("sites", sites);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
    }
    else if (_stricmp(cur.c_str(), "\\Shared with me") == 0)
    {
        std::vector<SharePointApi::SharePointItem> items;
        if (!forceRefresh && cache.GetFolder("sharedWithMe", items))
        {
            m_currentItems = items;
        }
        else
        {
            std::string err;
            if (api.ListSharedWithMe(items, &err))
            {
                m_currentItems = items;
                cache.PutFolder("sharedWithMe", items);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
    }
    else if (_stricmp(cur.c_str(), "\\Recent") == 0)
    {
        std::vector<SharePointApi::SharePointItem> items;
        if (!forceRefresh && cache.GetFolder("recent", items))
        {
            m_currentItems = items;
        }
        else
        {
            std::string err;
            if (api.ListRecent(items, &err))
            {
                m_currentItems = items;
                cache.PutFolder("recent", items);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
    }
    else if (_stricmp(cur.c_str(), "\\OneDrive") == 0)
    {
        std::vector<SharePointApi::SharePointItem> items;
        if (!forceRefresh && cache.GetFolder("onedrive:root", items))
        {
            m_currentItems = items;
        }
        else
        {
            std::string err;
            if (api.ListOneDriveRoot(items, &err))
            {
                m_currentItems = items;
                cache.PutFolder("onedrive:root", items);
            }
            else
            {
                SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                return FALSE;
            }
        }
    }
    else
    {
        std::string driveId, folderItemId, siteId;
        ResolveCurrentTarget(driveId, folderItemId, siteId);

        if (driveId.empty() && !siteId.empty())
        {
            std::vector<SharePointApi::SharePointItem> drives;
            std::string cacheKey = "site:" + siteId;
            if (!forceRefresh && cache.GetFolder(cacheKey, drives))
            {
                m_currentItems = drives;
            }
            else
            {
                std::string err;
                if (api.ListSiteDrives(siteId, drives, &err))
                {
                    m_currentItems = drives;
                    cache.PutFolder(cacheKey, drives);
                }
                else
                {
                    SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                    return FALSE;
                }
            }
        }
        else
        {
            if (driveId.empty() && cur.rfind("\\OneDrive", 0) == 0) driveId = "me";

            std::string cacheKey = "drive:" + driveId + ":folder:" + (folderItemId.empty() ? "root" : folderItemId);
            std::vector<SharePointApi::SharePointItem> items;

            if (!forceRefresh && cache.GetFolder(cacheKey, items))
            {
                m_currentItems = items;
            }
            else
            {
                std::string err;
                if (api.ListDriveChildren(driveId, folderItemId, items, &err))
                {
                    m_currentItems = items;
                    cache.PutFolder(cacheKey, items);
                }
                else
                {
                    SalamanderGeneral->SalMessageBox(NULL, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
                    return FALSE;
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_pathMapMutex);
        for (const auto& item : m_currentItems)
        {
            std::string subPath = cur;
            if (subPath.empty() || subPath.back() != '\\') subPath += "\\";
            subPath += item.name;

            s_pathToIdMap[subPath] = item.id;
            if (!item.driveId.empty()) s_pathToDriveMap[subPath] = item.driveId;
            if (!item.siteId.empty()) s_pathToSiteMap[subPath] = item.siteId;
        }
    }

    for (const auto& item : m_currentItems)
    {
        AddItemToDir(dir, item.name.c_str(), item.isFolder, item.size, !item.isFolder, &item.modifiedTime, pluginData, &item);
    }

    return TRUE;
}

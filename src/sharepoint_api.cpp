// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sharepoint_api.h"
#include "sharepoint_auth.h"
#include "sharepoint_json.h"
#include "sharepoint_log.h"
#include <iomanip>
#include <sstream>
#include <ctime>

namespace SharePointApi
{

ApiClient& ApiClient::GetInstance()
{
    static ApiClient instance;
    return instance;
}

FILETIME ApiClient::ParseIso8601ToFt(const std::string& iso)
{
    FILETIME ft = {0, 0};
    if (iso.empty()) return ft;

    SYSTEMTIME st{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) >= 6)
    {
        st.wYear = (WORD)year;
        st.wMonth = (WORD)month;
        st.wDay = (WORD)day;
        st.wHour = (WORD)hour;
        st.wMinute = (WORD)minute;
        st.wSecond = (WORD)second;
        st.wMilliseconds = 0;
        SystemTimeToFileTime(&st, &ft);
    }
    return ft;
}

SharePointItem ApiClient::ParseDriveItemJson(const SharePointJson::Value& v,
                                             const std::string& defaultDriveId,
                                             const std::string& defaultSiteId)
{
    SharePointItem item;
    item.id = v.GetString("id");
    item.name = v.GetString("name");
    item.size = v.GetInt64("size");
    item.isFolder = v.Has("folder");
    item.isTrashed = v.Has("deleted");
    item.webUrl = v.GetString("webUrl");
    item.downloadUrl = v.GetString("@microsoft.graph.downloadUrl");
    item.createdTime = ParseIso8601ToFt(v.GetString("createdDateTime"));
    item.modifiedTime = ParseIso8601ToFt(v.GetString("lastModifiedDateTime"));

    if (v.Has("createdBy"))
    {
        const auto& cb = v.GetObject("createdBy");
        if (cb.Has("user"))
        {
            item.authorName = cb.GetObject("user").GetString("displayName");
            item.authorEmail = cb.GetObject("user").GetString("email");
        }
    }

    if (v.Has("lastModifiedBy"))
    {
        const auto& mb = v.GetObject("lastModifiedBy");
        if (mb.Has("user"))
        {
            item.lastModifyingUserName = mb.GetObject("user").GetString("displayName");
            item.lastModifyingUserEmail = mb.GetObject("user").GetString("email");
        }
    }

    item.isShared = v.Has("shared");

    if (v.Has("parentReference"))
    {
        const auto& pr = v.GetObject("parentReference");
        item.parentId = pr.GetString("id");
        item.driveId = pr.GetString("driveId");
        item.siteId = pr.GetString("siteId");
        item.parentPath = pr.GetString("path");
    }

    if (item.driveId.empty()) item.driveId = defaultDriveId;
    if (item.siteId.empty()) item.siteId = defaultSiteId;

    return item;
}

bool ApiClient::GetStorageQuota(StorageQuota& quota, std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    auto resp = http.Get("https://graph.microsoft.com/v1.0/me/drive", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to fetch storage quota: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    if (json.Has("quota"))
    {
        const auto& q = json.GetObject("quota");
        quota.limit = q.GetInt64("total");
        quota.usage = q.GetInt64("used");
        quota.remaining = q.GetInt64("remaining");
        quota.deleted = q.GetInt64("deleted");
        return true;
    }

    return false;
}

bool ApiClient::ListSites(std::vector<SharePointItem>& sitesOut, std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::map<std::string, SharePointItem> siteMap;

    // 1. Root site
    auto rootResp = http.Get("https://graph.microsoft.com/v1.0/sites/root", token);
    if (rootResp.success)
    {
        auto json = SharePointJson::Value::Parse(rootResp.body);
        if (json.IsObject() && !json.GetString("id").empty())
        {
            SharePointItem site;
            site.id = json.GetString("id");
            site.name = json.GetString("displayName");
            if (site.name.empty()) site.name = json.GetString("name");
            if (site.name.empty()) site.name = "Root Site";
            site.isSite = true;
            site.isFolder = true;
            site.webUrl = json.GetString("webUrl");
            site.siteId = site.id;
            siteMap[site.id] = site;
        }
    }

    // 2. Search all accessible sites
    std::string searchUrl = "https://graph.microsoft.com/v1.0/sites?search=*&$top=100";
    while (!searchUrl.empty())
    {
        auto resp = http.Get(searchUrl, token);
        if (!resp.success) break;

        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                std::string id = v.GetString("id");
                if (!id.empty() && siteMap.find(id) == siteMap.end())
                {
                    SharePointItem site;
                    site.id = id;
                    site.name = v.GetString("displayName");
                    if (site.name.empty()) site.name = v.GetString("name");
                    site.isSite = true;
                    site.isFolder = true;
                    site.webUrl = v.GetString("webUrl");
                    site.siteId = id;
                    siteMap[id] = site;
                }
            }
        }
        searchUrl = json.GetString("@odata.nextLink");
    }

    // 3. Followed sites
    auto folResp = http.Get("https://graph.microsoft.com/v1.0/me/followedSites", token);
    if (folResp.success)
    {
        auto json = SharePointJson::Value::Parse(folResp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                std::string id = v.GetString("id");
                if (!id.empty() && siteMap.find(id) == siteMap.end())
                {
                    SharePointItem site;
                    site.id = id;
                    site.name = v.GetString("displayName");
                    if (site.name.empty()) site.name = v.GetString("name");
                    site.isSite = true;
                    site.isFolder = true;
                    site.webUrl = v.GetString("webUrl");
                    site.siteId = id;
                    siteMap[id] = site;
                }
            }
        }
    }

    for (const auto& [k, v] : siteMap)
    {
        sitesOut.push_back(v);
    }

    return true;
}

bool ApiClient::ListSiteDrives(const std::string& siteId,
                              std::vector<SharePointItem>& drivesOut,
                              std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string url = "https://graph.microsoft.com/v1.0/sites/" + siteId + "/drives";

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to list site document libraries: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    if (json.Has("value") && json["value"].IsArray())
    {
        for (const auto& v : json["value"].GetArrayElements())
        {
            SharePointItem drive;
            drive.id = v.GetString("id");
            drive.name = v.GetString("name");
            if (drive.name.empty()) drive.name = "Documents";
            drive.isDrive = true;
            drive.isFolder = true;
            drive.driveId = drive.id;
            drive.siteId = siteId;
            drive.webUrl = v.GetString("webUrl");
            drivesOut.push_back(drive);
        }
    }

    return true;
}

bool ApiClient::ListOneDriveRoot(std::vector<SharePointItem>& itemsOut, std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string nextUrl = "https://graph.microsoft.com/v1.0/me/drive/root/children?$top=200";

    while (!nextUrl.empty())
    {
        auto resp = http.Get(nextUrl, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list OneDrive items: " + resp.body;
            return false;
        }

        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                SharePointItem item = ParseDriveItemJson(v, "me");
                item.isOneDrive = true;
                itemsOut.push_back(item);
            }
        }

        nextUrl = json.GetString("@odata.nextLink");
    }

    return true;
}

bool ApiClient::ListDriveChildren(const std::string& driveId,
                                  const std::string& folderItemId,
                                  std::vector<SharePointItem>& itemsOut,
                                  std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string nextUrl;

    if (driveId == "me" || driveId.empty())
    {
        if (folderItemId.empty() || folderItemId == "root")
            nextUrl = "https://graph.microsoft.com/v1.0/me/drive/root/children?$top=200";
        else
            nextUrl = "https://graph.microsoft.com/v1.0/me/drive/items/" + folderItemId + "/children?$top=200";
    }
    else
    {
        if (folderItemId.empty() || folderItemId == "root")
            nextUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/root/children?$top=200";
        else
            nextUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + folderItemId + "/children?$top=200";
    }

    while (!nextUrl.empty())
    {
        auto resp = http.Get(nextUrl, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Failed to list folder contents: " + resp.body;
            return false;
        }

        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                itemsOut.push_back(ParseDriveItemJson(v, driveId));
            }
        }

        nextUrl = json.GetString("@odata.nextLink");
    }

    return true;
}

bool ApiClient::ListSharedWithMe(std::vector<SharePointItem>& itemsOut, std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    auto resp = http.Get("https://graph.microsoft.com/v1.0/me/drive/sharedWithMe", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to list Shared with me: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    if (json.Has("value") && json["value"].IsArray())
    {
        for (const auto& v : json["value"].GetArrayElements())
        {
            SharePointItem item = ParseDriveItemJson(v);
            item.isShared = true;
            itemsOut.push_back(item);
        }
    }

    return true;
}

bool ApiClient::ListRecent(std::vector<SharePointItem>& itemsOut, std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    auto resp = http.Get("https://graph.microsoft.com/v1.0/me/drive/recent", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Failed to list recent items: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    if (json.Has("value") && json["value"].IsArray())
    {
        for (const auto& v : json["value"].GetArrayElements())
        {
            itemsOut.push_back(ParseDriveItemJson(v));
        }
    }

    return true;
}

bool ApiClient::GetDriveDelta(const std::string& driveId,
                             const std::string& deltaTokenOrUrl,
                             std::vector<SharePointItem>& changedItemsOut,
                             std::string& newDeltaUrlOut,
                             std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string nextUrl;

    if (!deltaTokenOrUrl.empty() && deltaTokenOrUrl.rfind("http", 0) == 0)
    {
        nextUrl = deltaTokenOrUrl;
    }
    else
    {
        if (driveId == "me" || driveId.empty())
            nextUrl = "https://graph.microsoft.com/v1.0/me/drive/root/delta";
        else
            nextUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/root/delta";

        if (!deltaTokenOrUrl.empty())
        {
            nextUrl += "?token=" + SharePointHttp::HttpClient::UrlEncode(deltaTokenOrUrl);
        }
    }

    while (!nextUrl.empty())
    {
        auto resp = http.Get(nextUrl, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Delta query failed: " + resp.body;
            return false;
        }

        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                changedItemsOut.push_back(ParseDriveItemJson(v, driveId));
            }
        }

        if (json.Has("@odata.deltaLink"))
        {
            newDeltaUrlOut = json.GetString("@odata.deltaLink");
            nextUrl.clear();
        }
        else
        {
            nextUrl = json.GetString("@odata.nextLink");
        }
    }

    return true;
}

bool ApiClient::GetItemMetadata(const std::string& driveId,
                                const std::string& itemId,
                                SharePointItem& itemOut,
                                std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string url;
    if (driveId == "me" || driveId.empty())
        url = "https://graph.microsoft.com/v1.0/me/drive/items/" + itemId;
    else
        url = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + itemId;

    auto resp = http.Get(url, token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Get metadata failed: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    itemOut = ParseDriveItemJson(json, driveId);
    return true;
}

bool ApiClient::CreateFolder(const std::string& driveId,
                             const std::string& parentItemId,
                             const std::string& folderName,
                             SharePointItem& itemOut,
                             std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string url;

    if (driveId == "me" || driveId.empty())
    {
        if (parentItemId.empty() || parentItemId == "root")
            url = "https://graph.microsoft.com/v1.0/me/drive/root/children";
        else
            url = "https://graph.microsoft.com/v1.0/me/drive/items/" + parentItemId + "/children";
    }
    else
    {
        if (parentItemId.empty() || parentItemId == "root")
            url = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/root/children";
        else
            url = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + parentItemId + "/children";
    }

    SharePointJson::Value bodyVal;
    bodyVal.Set("name", SharePointJson::Value(folderName));
    bodyVal.Set("folder", SharePointJson::Value(SharePointJson::Type::Object));
    bodyVal.Set("@microsoft.graph.conflictBehavior", SharePointJson::Value("rename"));

    std::string body = bodyVal.Serialize();
    auto resp = http.Post(url, body, "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Create folder failed: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    itemOut = ParseDriveItemJson(json, driveId);
    return true;
}

bool ApiClient::RenameItem(const std::string& driveId,
                           const std::string& itemId,
                           const std::string& newName,
                           std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string url;
    if (driveId == "me" || driveId.empty())
        url = "https://graph.microsoft.com/v1.0/me/drive/items/" + itemId;
    else
        url = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + itemId;

    SharePointJson::Value bodyVal;
    bodyVal.Set("name", SharePointJson::Value(newName));

    auto resp = http.Patch(url, bodyVal.Serialize(), "application/json; charset=UTF-8", token);
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Rename item failed: " + resp.body;
        return false;
    }

    return true;
}

bool ApiClient::DeleteItem(const std::string& driveId,
                           const std::string& itemId,
                           std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string url;
    if (driveId == "me" || driveId.empty())
        url = "https://graph.microsoft.com/v1.0/me/drive/items/" + itemId;
    else
        url = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + itemId;

    auto resp = http.Delete(url, token);
    if (!resp.success && resp.statusCode != 204)
    {
        if (errorOut) *errorOut = "Delete item failed: " + resp.body;
        return false;
    }

    return true;
}

bool ApiClient::DownloadFile(const SharePointItem& item,
                             const std::wstring& targetLocalPath,
                             SharePointHttp::ProgressCallback progressCallback,
                             const bool* cancelFlag,
                             std::string* errorOut)
{
    SharePointHttp::HttpClient http;

    // 1. If item has pre-authenticated download URL, try direct download without bearer token
    if (!item.downloadUrl.empty())
    {
        std::string directErr;
        if (http.DownloadToFile(item.downloadUrl, targetLocalPath, "", progressCallback, cancelFlag, &directErr))
        {
            return true;
        }

        if (cancelFlag && *cancelFlag)
        {
            if (errorOut) *errorOut = directErr;
            return false;
        }
    }

    // 2. Fallback to Graph API /content endpoint with bearer token
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    std::string url;
    if (item.driveId == "me" || item.driveId.empty())
        url = "https://graph.microsoft.com/v1.0/me/drive/items/" + item.id + "/content";
    else
        url = "https://graph.microsoft.com/v1.0/drives/" + item.driveId + "/items/" + item.id + "/content";

    return http.DownloadToFile(url, targetLocalPath, token, progressCallback, cancelFlag, errorOut);
}

bool ApiClient::UploadFile(const std::string& driveId,
                           const std::string& parentFolderId,
                           const std::wstring& localFilePath,
                           const std::string& destFileName,
                           SharePointHttp::ProgressCallback progressCallback,
                           const bool* cancelFlag,
                           SharePointItem* itemOut,
                           std::string* errorOut,
                           bool replaceExisting)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;
    const char* conflictBehavior = replaceExisting ? "replace" : "fail";

    HANDLE hFile = CreateFileW(localFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (errorOut) *errorOut = "Failed to open local file";
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize))
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "Failed to query file size";
        return false;
    }

    SharePointHttp::HttpClient http;
    std::string encName = SharePointHttp::HttpClient::UrlEncode(destFileName);

    // If file size is smaller than 4MB, use Simple Upload
    if (fileSize.QuadPart < 4 * 1024 * 1024)
    {
        CloseHandle(hFile);

        std::string uploadUrl;
        if (driveId == "me" || driveId.empty())
        {
            if (parentFolderId.empty() || parentFolderId == "root")
                uploadUrl = "https://graph.microsoft.com/v1.0/me/drive/root:/" + encName + ":/content";
            else
                uploadUrl = "https://graph.microsoft.com/v1.0/me/drive/items/" + parentFolderId + ":/" + encName + ":/content";
        }
        else
        {
            if (parentFolderId.empty() || parentFolderId == "root")
                uploadUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/root:/" + encName + ":/content";
            else
                uploadUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + parentFolderId + ":/" + encName + ":/content";
        }

        // Explicit conflict behavior (Graph default would silently replace).
        uploadUrl += std::string("?@microsoft.graph.conflictBehavior=") + conflictBehavior;

        std::string respBody;
        bool ok = http.UploadSimpleFile(uploadUrl, localFilePath, "application/octet-stream",
                                        token, progressCallback, cancelFlag, &respBody, errorOut);
        if (ok && itemOut && !respBody.empty())
        {
            auto json = SharePointJson::Value::Parse(respBody);
            *itemOut = ParseDriveItemJson(json, driveId);
        }
        return ok;
    }

    // Large file upload session (> 4MB)
    std::string sessionUrl;
    if (driveId == "me" || driveId.empty())
    {
        if (parentFolderId.empty() || parentFolderId == "root")
            sessionUrl = "https://graph.microsoft.com/v1.0/me/drive/root:/" + encName + ":/createUploadSession";
        else
            sessionUrl = "https://graph.microsoft.com/v1.0/me/drive/items/" + parentFolderId + ":/" + encName + ":/createUploadSession";
    }
    else
    {
        if (parentFolderId.empty() || parentFolderId == "root")
            sessionUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/root:/" + encName + ":/createUploadSession";
        else
            sessionUrl = "https://graph.microsoft.com/v1.0/drives/" + driveId + "/items/" + parentFolderId + ":/" + encName + ":/createUploadSession";
    }

    SharePointJson::Value sessionReq;
    SharePointJson::Value itemObj(SharePointJson::Type::Object);
    itemObj.Set("@microsoft.graph.conflictBehavior", SharePointJson::Value(conflictBehavior));
    itemObj.Set("name", SharePointJson::Value(destFileName));
    sessionReq.Set("item", itemObj);

    auto sessResp = http.Post(sessionUrl, sessionReq.Serialize(), "application/json; charset=UTF-8", token);
    if (!sessResp.success)
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "Create upload session failed: " + sessResp.body;
        return false;
    }

    auto sessJson = SharePointJson::Value::Parse(sessResp.body);
    std::string uploadUrl = sessJson.GetString("uploadUrl");
    if (uploadUrl.empty())
    {
        CloseHandle(hFile);
        if (errorOut) *errorOut = "No uploadUrl returned by createUploadSession";
        return false;
    }

    // Chunk size: 320 KB * 10 = 3,276,800 bytes (must be multiple of 320 KB)
    const size_t chunkSize = 320 * 1024 * 10;
    std::vector<unsigned char> chunkBuf(chunkSize);
    int64_t totalBytes = fileSize.QuadPart;
    int64_t bytesUploaded = 0;
    bool uploadSuccess = true;

    while (bytesUploaded < totalBytes)
    {
        if (cancelFlag && *cancelFlag)
        {
            uploadSuccess = false;
            if (errorOut) *errorOut = "Upload cancelled by user";
            break;
        }

        size_t currentChunk = (size_t)(std::min)((int64_t)chunkSize, totalBytes - bytesUploaded);
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, chunkBuf.data(), (DWORD)currentChunk, &bytesRead, NULL) || bytesRead != currentChunk)
        {
            uploadSuccess = false;
            if (errorOut) *errorOut = "Failed to read local file chunk";
            break;
        }

        int64_t startByte = bytesUploaded;
        int64_t endByte = bytesUploaded + bytesRead - 1;
        std::string rangeHeader = "bytes " + std::to_string(startByte) + "-" +
                                  std::to_string(endByte) + "/" + std::to_string(totalBytes);

        auto chunkResp = http.UploadChunk(uploadUrl, chunkBuf.data(), bytesRead, rangeHeader, "");
        if (!chunkResp.success && chunkResp.statusCode != 200 && chunkResp.statusCode != 201 && chunkResp.statusCode != 202)
        {
            uploadSuccess = false;
            if (errorOut) *errorOut = "Chunk upload failed (HTTP " + std::to_string(chunkResp.statusCode) + "): " + chunkResp.body;
            break;
        }

        bytesUploaded += bytesRead;
        if (progressCallback)
        {
            if (!progressCallback(bytesUploaded, totalBytes))
            {
                uploadSuccess = false;
                if (errorOut) *errorOut = "Upload cancelled by user";
                break;
            }
        }

        if (bytesUploaded == totalBytes && itemOut && !chunkResp.body.empty())
        {
            auto itemJson = SharePointJson::Value::Parse(chunkResp.body);
            *itemOut = ParseDriveItemJson(itemJson, driveId);
        }
    }

    CloseHandle(hFile);
    return uploadSuccess;
}

bool ApiClient::SearchFiles(const SearchOptions& opts,
                            std::vector<SharePointItem>& resultsOut,
                            const std::atomic<bool>* cancelFlag,
                            std::string* errorOut)
{
    std::string token = SharePointAuth::AuthManager::GetInstance().GetValidAccessToken(errorOut);
    if (token.empty()) return false;

    SharePointHttp::HttpClient http;
    std::string searchUrl;
    std::string encQuery = SharePointHttp::HttpClient::UrlEncode(opts.queryNamed);

    if (!opts.driveId.empty())
    {
        searchUrl = "https://graph.microsoft.com/v1.0/drives/" + opts.driveId + "/root/search(q='" + encQuery + "')";
    }
    else if (!opts.siteId.empty())
    {
        searchUrl = "https://graph.microsoft.com/v1.0/sites/" + opts.siteId + "/drive/root/search(q='" + encQuery + "')";
    }
    else
    {
        searchUrl = "https://graph.microsoft.com/v1.0/me/drive/root/search(q='" + encQuery + "')";
    }

    while (!searchUrl.empty())
    {
        if (cancelFlag && cancelFlag->load())
        {
            if (errorOut) *errorOut = "Search cancelled";
            return false;
        }

        auto resp = http.Get(searchUrl, token);
        if (!resp.success)
        {
            if (errorOut) *errorOut = "Search failed: " + resp.body;
            return false;
        }

        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.Has("value") && json["value"].IsArray())
        {
            for (const auto& v : json["value"].GetArrayElements())
            {
                SharePointItem item = ParseDriveItemJson(v, opts.driveId, opts.siteId);

                // Type filter
                if (opts.typeFilter == 6 && !item.isFolder) continue; // Folders only
                if (opts.typeFilter > 0 && opts.typeFilter < 6 && item.isFolder) continue;

                resultsOut.push_back(item);
            }
        }

        searchUrl = json.GetString("@odata.nextLink");
    }

    return true;
}

} // namespace SharePointApi

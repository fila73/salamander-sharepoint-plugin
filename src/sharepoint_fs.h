// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "sharepoint_api.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>

#define TOP_INDEX_MEM_SIZE 30

inline constexpr const char* kOneDriveDir = "OneDrive";
inline constexpr const char* kSitesDir = "Sites";
inline constexpr const char* kSharedWithMeDir = "Shared with me";
inline constexpr const char* kRecentDir = "Recent";
inline constexpr const char* kTrashDir = "Recycle Bin";

struct CTopIndexMem
{
    char Path[MAX_PATH];
    int TopIndexes[TOP_INDEX_MEM_SIZE];
    int TopIndexesCount;

    CTopIndexMem()
    {
        Path[0] = 0;
        memset(TopIndexes, 0, sizeof(TopIndexes));
        TopIndexesCount = 0;
    }

    void Push(const char* path, int topIndex)
    {
        const char* s = path + strlen(path);
        if (s > path && *(s - 1) == '\\')
            s--;
        BOOL ok;
        if (s == path)
            ok = FALSE;
        else
        {
            if (s > path && *s == '\\')
                s--;
            while (s > path && *s != '\\')
                s--;

            int l = (int)strlen(Path);
            if (l > 0 && Path[l - 1] == '\\')
                l--;
            ok = (s - path == l) && (_strnicmp(path, Path, l) == 0);
        }

        if (ok)
        {
            if (TopIndexesCount == TOP_INDEX_MEM_SIZE)
            {
                for (int i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                    TopIndexes[i] = TopIndexes[i + 1];
                TopIndexesCount--;
            }
            lstrcpynA(Path, path, MAX_PATH);
            TopIndexes[TopIndexesCount++] = topIndex;
        }
        else
        {
            lstrcpynA(Path, path, MAX_PATH);
            TopIndexesCount = 1;
            TopIndexes[0] = topIndex;
        }
    }

    BOOL FindAndPop(const char* path, int& topIndex)
    {
        if (TopIndexesCount > 0 && _stricmp(Path, path) == 0)
        {
            topIndex = TopIndexes[--TopIndexesCount];
            if (TopIndexesCount > 0)
            {
                char* s = Path + strlen(Path);
                if (s > Path && *(s - 1) == '\\')
                    s--;
                while (s > Path && *s != '\\')
                    s--;
                if (s > Path && *s == '\\')
                    *s = 0;
                else
                    Path[0] = 0;
            }
            else
                Path[0] = 0;
            return TRUE;
        }
        return FALSE;
    }
};

class CSharePointPluginDataInterface : public CPluginDataInterfaceAbstract
{
public:
    virtual BOOL WINAPI CallReleaseForFiles() override { return TRUE; }
    virtual BOOL WINAPI CallReleaseForDirs() override { return TRUE; }
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir) override;
    virtual void WINAPI GetFileDataForUpDir(const char* archivePath, CFileData& upDir) override {}
    virtual BOOL WINAPI GetFileDataForNewDir(const char* dirName, CFileData& dir) override { return FALSE; }
    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) override { return NULL; }
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) override { return FALSE; }
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) override { destroyIcon = FALSE; return NULL; }
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) override;

    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                  const char* archivePath, const CFileData* upperDir) override;
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth) override;
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth) override;

    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           char* buffer, DWORD* hotTexts, int& hotTextsCount) override { return FALSE; }
    virtual BOOL WINAPI CanBeCopiedToClipboard() override { return FALSE; }
    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) override { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) override { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) override { return FALSE; }
};

class CPluginFS : public CPluginFSInterfaceAbstract
{
public:
    CTopIndexMem TopIndexMem;

    CPluginFS(const char* fsName);
    virtual ~CPluginFS();

    virtual BOOL WINAPI GetCurrentPath(char* userPart) override;
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, char* buf, int bufSize) override;
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success) override;
    virtual BOOL WINAPI GetRootPath(char* userPart) override;

    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) override;
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) override;

    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                   const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode) override;
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh) override;
    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason) override;
    virtual void WINAPI Event(int event, DWORD param) override;
    virtual void WINAPI ReleaseObject(HWND parent) override;

    virtual DWORD WINAPI GetSupportedServices() override;

    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const char* fsName, char*& title,
                                                       HICON& icon, BOOL& destroyIcon) override;
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon) override;
    virtual void WINAPI GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) override;
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) override;
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset) override;
    virtual void WINAPI CompleteDirectoryLineHotPath(char* path, int pathBufSize) override;
    virtual BOOL WINAPI GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize) override;
    virtual void WINAPI ShowInfoDialog(const char* fsName, HWND parent) override;
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo) override;
    virtual BOOL WINAPI QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                    BOOL isDir, char* newName, BOOL& cancel) override;
    virtual void WINAPI AcceptChangeOnPathNotification(const char* fsName, const char* path,
                                                       BOOL includingSubdirs) override;
    virtual BOOL WINAPI CreateDir(const char* fsName, int mode, HWND parent,
                                  char* newName, BOOL& cancel) override;
    virtual void WINAPI ViewFile(const char* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) override;
    virtual BOOL WINAPI Delete(const char* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) override;
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         char* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) override;
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                               const char* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int selectedFiles, int selectedDirs,
                                               char* targetPath, BOOL* cancelOrHandlePath) override;
    virtual BOOL WINAPI ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) override;
    virtual void WINAPI ShowProperties(const char* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) override;
    virtual void WINAPI ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) override;
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) override;
    virtual BOOL WINAPI OpenFindDialog(const char* fsName, int panel) override;
    virtual void WINAPI OpenActiveFolder(const char* fsName, HWND parent) override;
    virtual void WINAPI GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects) override;
    virtual BOOL WINAPI GetNoItemsInPanelText(char* textBuf, int textBufSize) override;
    virtual void WINAPI ShowSecurityInfo(HWND parent) override;

    bool ResolveCurrentTarget(std::string& outDriveId, std::string& outFolderItemId, std::string& outSiteId);
    std::string GetCurrentPathStr() const { return m_currentPath; }
    std::string FindItemByPanelName(const char* name, BOOL isDir);
    const SharePointApi::SharePointItem* FindItemObjectByPanelName(const char* name, BOOL isDir) const;

private:
    std::string m_fsName;
    std::string m_currentPath = "\\";
    std::vector<SharePointApi::SharePointItem> m_currentItems;
};

class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
public:
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const char* fsName, int fsNameIndex) override;
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) override;

    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) override;
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const char* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) override;
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) override;
    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const char* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir) override;
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const char* pluginFSName, int pluginFSNameIndex) override;

    virtual void WINAPI ConvertPathToInternal(const char* fsName, int fsNameIndex, char* fsUserPart) override;
    virtual void WINAPI ConvertPathToExternal(const char* fsName, int fsNameIndex, char* fsUserPart) override;
    virtual void WINAPI EnsureShareExistsOnServer(int panel, const char* server, const char* share) override {}
};

extern CPluginInterfaceForFS InterfaceForFS;

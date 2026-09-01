// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "sharepoint_api.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

class CSharePointFindDialog
{
public:
    CSharePointFindDialog(HWND hParent, int panel, const std::string& currentPath);
    ~CSharePointFindDialog();

    static void Launch(HWND hParent, int panel, const std::string& currentPath);
    static void CloseIfOpen();

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnInitDialog(HWND hDlg);
    void OnCommand(HWND hDlg, int id, HWND hCtrl, UINT codeNotify);
    void OnSize(HWND hDlg, UINT state, int cx, int cy);
    void OnDestroy(HWND hDlg);

    void StartSearch();
    void StopSearch();
    void SearchWorker();

    void PopulateResults(const std::vector<SharePointApi::SharePointItem>& items);
    void FocusSelectedItem();
    void ViewSelectedItem();
    void OpenWebSelectedItem();
    void CopyLinkSelectedItem();

    HWND m_hDlg = NULL;
    HWND m_hParent = NULL;
    int m_panel = 0;
    std::string m_initialPath;

    HWND m_hNamed = NULL;
    HWND m_hLookIn = NULL;
    HWND m_hResultsList = NULL;
    HWND m_hStatus = NULL;
    HWND m_hBtnFindNow = NULL;
    HWND m_hBtnStop = NULL;
    HWND m_hBtnFocus = NULL;
    HWND m_hBtnView = NULL;
    HWND m_hBtnOpenWeb = NULL;
    HWND m_hBtnCopyLink = NULL;

    std::vector<SharePointApi::SharePointItem> m_results;
    std::mutex m_resultsMutex;
    std::thread m_searchThread;
    std::atomic<bool> m_isSearching{false};
    std::atomic<bool> m_cancelRequested{false};
    SharePointApi::SearchOptions m_currentSearchOpts;
};

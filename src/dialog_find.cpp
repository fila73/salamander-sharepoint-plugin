// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dialog_find.h"
#include "dialogs.h"
#include "sharepoint.h"
#include "sharepoint.rh2"
#include "lang.rh"
#include "sharepoint_log.h"
#include "sharepointdarkmode.h"
#include "sharepoint_auth.h"
#include "sharepoint_http.h"
#include "sharepoint_cache.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>

#define WM_USER_SEARCH_FINISHED (WM_USER + 101)

static CSharePointFindDialog* s_activeFindDialog = nullptr;

CSharePointFindDialog::CSharePointFindDialog(HWND hParent, int panel, const std::string& currentPath)
    : m_hParent(hParent),
      m_panel(panel),
      m_initialPath(currentPath)
{
}

CSharePointFindDialog::~CSharePointFindDialog()
{
    StopSearch();
    if (m_searchThread.joinable())
    {
        m_searchThread.join();
    }
}

void CSharePointFindDialog::Launch(HWND hParent, int panel, const std::string& currentPath)
{
    if (s_activeFindDialog && s_activeFindDialog->m_hDlg && IsWindow(s_activeFindDialog->m_hDlg))
    {
        SetForegroundWindow(s_activeFindDialog->m_hDlg);
        return;
    }

    if (s_activeFindDialog)
    {
        delete s_activeFindDialog;
        s_activeFindDialog = nullptr;
    }

    s_activeFindDialog = new CSharePointFindDialog(hParent, panel, currentPath);
    HWND hWnd = CreateDialogParam(HLanguage ? HLanguage : DLLInstance,
                                  MAKEINTRESOURCE(IDD_FIND),
                                  NULL,
                                  DialogProc,
                                  (LPARAM)s_activeFindDialog);
    if (!hWnd)
    {
        delete s_activeFindDialog;
        s_activeFindDialog = nullptr;
        return;
    }

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
}

void CSharePointFindDialog::CloseIfOpen()
{
    if (s_activeFindDialog && s_activeFindDialog->m_hDlg && IsWindow(s_activeFindDialog->m_hDlg))
    {
        s_activeFindDialog->StopSearch();
        if (s_activeFindDialog->m_searchThread.joinable())
            s_activeFindDialog->m_searchThread.join();

        DestroyWindow(s_activeFindDialog->m_hDlg);
        MSG msg;
        while (s_activeFindDialog && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (s_activeFindDialog)
    {
        delete s_activeFindDialog;
        s_activeFindDialog = nullptr;
    }
}

INT_PTR CALLBACK CSharePointFindDialog::DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CSharePointFindDialog* pThis = nullptr;
    if (uMsg == WM_INITDIALOG)
    {
        pThis = reinterpret_cast<CSharePointFindDialog*>(lParam);
        SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hDlg = hDlg;
    }
    else
    {
        pThis = reinterpret_cast<CSharePointFindDialog*>(GetWindowLongPtr(hDlg, DWLP_USER));
    }

    if (pThis)
    {
        return pThis->HandleMessage(hDlg, uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR CSharePointFindDialog::HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR colorRes = 0;
    if (SharePointDarkMode::HandleDialogColors(uMsg, wParam, lParam, &colorRes))
    {
        return colorRes;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        OnInitDialog(hDlg);
        return TRUE;

    case WM_CLOSE:
        StopSearch();
        DestroyWindow(hDlg);
        return TRUE;

    case WM_COMMAND:
        OnCommand(hDlg, LOWORD(wParam), (HWND)lParam, HIWORD(wParam));
        return TRUE;

    case WM_USER_SEARCH_FINISHED:
        m_isSearching = false;
        if (m_hBtnFindNow) EnableWindow(m_hBtnFindNow, TRUE);
        if (m_hBtnStop) EnableWindow(m_hBtnStop, FALSE);

        if (m_hStatus)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), LoadStr(IDS_FIND_FINISHED_FMT), (int)m_results.size());
            SetWindowTextA(m_hStatus, buf);
        }
        return TRUE;

    case WM_NCDESTROY:
        if (s_activeFindDialog == this)
        {
            s_activeFindDialog = nullptr;
        }
        delete this;
        return TRUE;
    }

    return FALSE;
}

void CSharePointFindDialog::OnInitDialog(HWND hDlg)
{
    SharePointDarkMode::ApplyWindowTheme(hDlg);

    m_hNamed = GetDlgItem(hDlg, IDC_FIND_NAMED);
    m_hLookIn = GetDlgItem(hDlg, IDC_FIND_LOOKIN);
    m_hResultsList = GetDlgItem(hDlg, IDC_FIND_RESULTS);
    m_hStatus = GetDlgItem(hDlg, IDC_FIND_STATUS);
    m_hBtnFindNow = GetDlgItem(hDlg, IDC_FIND_BTN_FINDNOW);
    m_hBtnStop = GetDlgItem(hDlg, IDC_FIND_BTN_STOP);
    m_hBtnFocus = GetDlgItem(hDlg, IDC_FIND_BTN_FOCUS);
    m_hBtnView = GetDlgItem(hDlg, IDC_FIND_BTN_VIEW);
    m_hBtnOpenWeb = GetDlgItem(hDlg, IDC_FIND_BTN_OPENWEB);
    m_hBtnCopyLink = GetDlgItem(hDlg, IDC_FIND_BTN_COPYLINK);

    if (m_hResultsList)
    {
        ListView_SetExtendedListViewStyle(m_hResultsList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        SharePointDarkMode::ApplyListViewTheme(m_hResultsList);

        LVCOLUMNA lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        lvc.iSubItem = 0;
        lvc.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_NAME));
        lvc.cx = 220;
        ListView_InsertColumn(m_hResultsList, 0, &lvc);

        lvc.iSubItem = 1;
        lvc.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_PATH));
        lvc.cx = 250;
        ListView_InsertColumn(m_hResultsList, 1, &lvc);

        lvc.iSubItem = 2;
        lvc.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_SIZE));
        lvc.cx = 80;
        ListView_InsertColumn(m_hResultsList, 2, &lvc);

        lvc.iSubItem = 3;
        lvc.pszText = const_cast<char*>(LoadStr(IDS_FIND_COL_AUTHOR));
        lvc.cx = 120;
        ListView_InsertColumn(m_hResultsList, 3, &lvc);
    }

    if (m_hLookIn)
    {
        SendMessageA(m_hLookIn, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_FIND_LOOKIN_ALL));
        SendMessage(m_hLookIn, CB_SETCURSEL, 0, 0);
    }

    if (m_hBtnStop) EnableWindow(m_hBtnStop, FALSE);
    if (m_hStatus) SetWindowTextA(m_hStatus, LoadStr(IDS_FIND_READY));
}

void CSharePointFindDialog::OnCommand(HWND hDlg, int id, HWND hCtrl, UINT codeNotify)
{
    switch (id)
    {
    case IDC_FIND_BTN_FINDNOW:
        StartSearch();
        break;

    case IDC_FIND_BTN_STOP:
        StopSearch();
        break;

    case IDCANCEL:
        if (m_isSearching)
        {
            StopSearch();
        }
        else
        {
            DestroyWindow(hDlg);
        }
        break;

    case IDC_FIND_BTN_FOCUS:
        FocusSelectedItem();
        break;

    case IDC_FIND_BTN_VIEW:
        ViewSelectedItem();
        break;

    case IDC_FIND_BTN_OPENWEB:
        OpenWebSelectedItem();
        break;

    case IDC_FIND_BTN_COPYLINK:
        CopyLinkSelectedItem();
        break;
    }
}

void CSharePointFindDialog::StartSearch()
{
    if (m_isSearching) return;

    char szNamed[512] = {0};
    GetDlgItemTextA(m_hDlg, IDC_FIND_NAMED, szNamed, sizeof(szNamed));

    m_currentSearchOpts = SharePointApi::SearchOptions();
    m_currentSearchOpts.queryNamed = szNamed;

    m_results.clear();
    if (m_hResultsList) ListView_DeleteAllItems(m_hResultsList);

    m_isSearching = true;
    m_cancelRequested = false;

    if (m_hBtnFindNow) EnableWindow(m_hBtnFindNow, FALSE);
    if (m_hBtnStop) EnableWindow(m_hBtnStop, TRUE);
    if (m_hStatus) SetWindowTextA(m_hStatus, LoadStr(IDS_FIND_SEARCHING));

    if (m_searchThread.joinable()) m_searchThread.join();
    m_searchThread = std::thread(&CSharePointFindDialog::SearchWorker, this);
}

void CSharePointFindDialog::StopSearch()
{
    m_cancelRequested = true;
}

void CSharePointFindDialog::SearchWorker()
{
    std::vector<SharePointApi::SharePointItem> items;
    std::string err;
    SharePointApi::ApiClient::GetInstance().SearchFiles(m_currentSearchOpts, items, &m_cancelRequested, &err);

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = items;
    }

    if (m_hResultsList)
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            LVITEMA lvi{};
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = (int)i;
            lvi.iSubItem = 0;
            lvi.pszText = const_cast<char*>(items[i].name.c_str());
            lvi.lParam = (LPARAM)i;
            int idx = ListView_InsertItem(m_hResultsList, &lvi);

            ListView_SetItemText(m_hResultsList, idx, 1, const_cast<char*>(items[i].parentPath.c_str()));

            char szSize[64];
            if (items[i].isFolder)
                strcpy(szSize, "<DIR>");
            else
                snprintf(szSize, sizeof(szSize), "%lld KB", (long long)(items[i].size / 1024));
            ListView_SetItemText(m_hResultsList, idx, 2, szSize);

            ListView_SetItemText(m_hResultsList, idx, 3, const_cast<char*>(items[i].authorName.c_str()));
        }
    }

    if (m_hDlg && IsWindow(m_hDlg))
    {
        PostMessage(m_hDlg, WM_USER_SEARCH_FINISHED, 0, 0);
    }
}

void CSharePointFindDialog::FocusSelectedItem()
{
}

void CSharePointFindDialog::ViewSelectedItem()
{
}

void CSharePointFindDialog::OpenWebSelectedItem()
{
    if (!m_hResultsList) return;
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)m_results.size())
    {
        if (!m_results[sel].webUrl.empty())
        {
            ShellExecuteA(NULL, "open", m_results[sel].webUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

void CSharePointFindDialog::CopyLinkSelectedItem()
{
    if (!m_hResultsList) return;
    int sel = ListView_GetNextItem(m_hResultsList, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)m_results.size())
    {
        const auto& url = m_results[sel].webUrl;
        if (!url.empty() && OpenClipboard(m_hDlg))
        {
            EmptyClipboard();
            HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, url.length() + 1);
            if (hGlob)
            {
                memcpy(GlobalLock(hGlob), url.c_str(), url.length() + 1);
                GlobalUnlock(hGlob);
                SetClipboardData(CF_TEXT, hGlob);
            }
            CloseClipboard();
        }
    }
}

// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dialogs.h"
#include "sharepoint_auth.h"
#include "sharepoint.h"
#include "sharepoint_api.h"
#include "sharepoint_cache.h"
#include "sharepoint_log.h"

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, hParent, origin)
{
}

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, helpID, hParent, origin)
{
}

INT_PTR CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR colorResult = 0;
    if (SharePointDarkMode::HandleDialogColors(uMsg, wParam, lParam, &colorResult))
    {
        return colorResult;
    }

    if (uMsg == WM_INITDIALOG)
    {
        SharePointDarkMode::ApplyWindowTheme(HWindow);
        if (Parent != NULL)
        {
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CCommonDialog::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

INT_PTR CCommonPropSheetPage::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR colorResult = 0;
    if (SharePointDarkMode::HandleDialogColors(uMsg, wParam, lParam, &colorResult))
    {
        return colorResult;
    }

    if (uMsg == WM_INITDIALOG)
    {
        SharePointDarkMode::ApplyWindowTheme(HWindow);
    }
    return CPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// CConfigPageAccounts
//

CConfigPageAccounts::CConfigPageAccounts()
    : CCommonPropSheetPage(LoadStr(IDS_CFG_PAGE_ACCOUNTS), HLanguage, IDD_CFGPAGEACCOUNTS, IDD_CFGPAGEACCOUNTS, PSP_HASHELP, NULL)
{
}

void CConfigPageAccounts::RefreshAccountsList()
{
    HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
    if (!hCombo) return;

    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);

    auto accounts = SharePointAuth::AuthManager::GetInstance().GetAccounts();
    int activeIndex = -1;

    for (size_t i = 0; i < accounts.size(); ++i)
    {
        std::string display = accounts[i].email;
        if (!accounts[i].displayName.empty() && accounts[i].displayName != accounts[i].email)
        {
            display += " (" + accounts[i].displayName + ")";
        }
        if (accounts[i].isActive)
        {
            display += " [Active]";
            activeIndex = (int)i;
        }

        int idx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)display.c_str());
        SendMessage(hCombo, CB_SETITEMDATA, idx, (LPARAM)i);
    }

    if (accounts.empty())
    {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_NOT_LOGGED_IN));
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_SWITCH_BTN), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_REMOVE_BTN), FALSE);
    }
    else
    {
        SendMessage(hCombo, CB_SETCURSEL, activeIndex >= 0 ? activeIndex : 0, 0);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_SWITCH_BTN), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_CFG_ACCOUNT_REMOVE_BTN), TRUE);
    }
}

void CConfigPageAccounts::Transfer(CTransferInfo& ti)
{
    char szClientId[512] = {0};
    char szTenantId[512] = {0};

    if (ti.Type == ttDataToWindow)
    {
        std::string id = SharePointAuth::AuthManager::GetInstance().GetClientId();
        strncpy(szClientId, id.c_str(), sizeof(szClientId) - 1);
        std::string tenant = SharePointAuth::AuthManager::GetInstance().GetTenantId();
        strncpy(szTenantId, tenant.c_str(), sizeof(szTenantId) - 1);
    }

    ti.EditLine(IDC_CFG_CLIENTID, szClientId, sizeof(szClientId));
    ti.EditLine(IDC_CFG_TENANTID, szTenantId, sizeof(szTenantId));

    if (ti.Type == ttDataFromWindow)
    {
        SharePointAuth::AuthManager::GetInstance().SetClientId(szClientId);
        SharePointAuth::AuthManager::GetInstance().SetTenantId(szTenantId);
    }
}

INT_PTR CConfigPageAccounts::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        RefreshAccountsList();
    }
    else if (uMsg == WM_COMMAND)
    {
        WORD ctrlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (ctrlId == IDC_CFG_ACCOUNT_ADD_BTN && notifyCode == BN_CLICKED)
        {
            char szId[512] = {0};
            char szTenant[512] = {0};
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, szId, sizeof(szId));
            GetDlgItemTextA(HWindow, IDC_CFG_TENANTID, szTenant, sizeof(szTenant));

            SharePointAuth::AuthManager::GetInstance().SetClientId(szId);
            SharePointAuth::AuthManager::GetInstance().SetTenantId(szTenant);

            std::string err;
            if (SharePointAuth::AuthManager::GetInstance().AddAccount(HWindow, &err))
            {
                RefreshAccountsList();
                SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_STATUS_AUTH_SUCCESS), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            }
            else if (!err.empty())
            {
                SalamanderGeneral->SalMessageBox(HWindow, err.c_str(), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNT_SWITCH_BTN && notifyCode == BN_CLICKED)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR)
            {
                auto accounts = SharePointAuth::AuthManager::GetInstance().GetAccounts();
                int idx = (int)SendMessage(hCombo, CB_GETITEMDATA, sel, 0);
                if (idx >= 0 && idx < (int)accounts.size())
                {
                    SharePointAuth::AuthManager::GetInstance().SwitchAccount(accounts[idx].email);
                    RefreshAccountsList();
                }
            }
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNT_REMOVE_BTN && notifyCode == BN_CLICKED)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_ACCOUNTS_COMBO);
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR)
            {
                auto accounts = SharePointAuth::AuthManager::GetInstance().GetAccounts();
                int idx = (int)SendMessage(hCombo, CB_GETITEMDATA, sel, 0);
                if (idx >= 0 && idx < (int)accounts.size())
                {
                    if (SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CONFIRM_REMOVE_ACCOUNT),
                                                         LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONQUESTION) == IDYES)
                    {
                        SharePointAuth::AuthManager::GetInstance().RemoveAccount(accounts[idx].email);
                        RefreshAccountsList();
                    }
                }
            }
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_ACCOUNT_SAVE_BTN && notifyCode == BN_CLICKED)
        {
            char szId[512] = {0};
            char szTenant[512] = {0};
            GetDlgItemTextA(HWindow, IDC_CFG_CLIENTID, szId, sizeof(szId));
            GetDlgItemTextA(HWindow, IDC_CFG_TENANTID, szTenant, sizeof(szTenant));

            SharePointAuth::AuthManager::GetInstance().SetClientId(szId);
            SharePointAuth::AuthManager::GetInstance().SetTenantId(szTenant);
            SharePointAuth::AuthManager::GetInstance().SaveTokens();

            SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_KEYS_SAVED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// CConfigPageDisplay
//

CConfigPageDisplay::CConfigPageDisplay()
    : CCommonPropSheetPage(LoadStr(IDS_CFG_PAGE_DISPLAY), HLanguage, IDD_CFGPAGEDISPLAY, IDD_CFGPAGEDISPLAY, PSP_HASHELP, NULL)
{
}

void CConfigPageDisplay::Transfer(CTransferInfo& ti)
{
    BOOL logging = SharePointLog::IsFileLoggingEnabled() ? TRUE : FALSE;
    ti.CheckBox(IDC_CFG_DEBUG_LOGGING, logging);
    if (ti.Type == ttDataFromWindow)
    {
        SharePointLog::SetFileLoggingEnabled(logging != FALSE);
    }
}

INT_PTR CConfigPageDisplay::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// CConfigPageCache
//

CConfigPageCache::CConfigPageCache()
    : CCommonPropSheetPage(LoadStr(IDS_CFG_PAGE_CACHE), HLanguage, IDD_CFGPAGECACHE, IDD_CFGPAGECACHE, PSP_HASHELP, NULL)
{
}

void CConfigPageCache::UpdateCacheStatus()
{
    HWND hStatus = GetDlgItem(HWindow, IDC_CFG_CACHE_STATUS);
    if (hStatus)
    {
        std::string acc = SharePointAuth::AuthManager::GetInstance().GetTokens().accountEmail;
        if (!acc.empty())
        {
            char msg[512];
            snprintf(msg, sizeof(msg), LoadStr(IDS_CFG_CACHE_ACTIVE_FMT), acc.c_str());
            SetWindowTextA(hStatus, msg);
        }
        else
        {
            SetWindowTextA(hStatus, LoadStr(IDS_CFG_CACHE_NO_ACTIVE));
        }
    }
}

void CConfigPageCache::Transfer(CTransferInfo& ti)
{
    BOOL cacheEnabled = SharePointCache::CacheManager::GetInstance().IsEnabled() ? TRUE : FALSE;
    ti.CheckBox(IDC_CFG_CACHE_ENABLED, cacheEnabled);
    if (ti.Type == ttDataFromWindow)
    {
        SharePointCache::CacheManager::GetInstance().SetEnabled(cacheEnabled != FALSE);
    }
}

INT_PTR CConfigPageCache::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        HWND hCombo = GetDlgItem(HWindow, IDC_CFG_CACHE_INTERVAL_COMBO);
        if (hCombo)
        {
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_10SEC));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_30SEC));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_1MIN));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_5MIN));
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_TTL_MANUAL));

            DWORD interval = SharePointCache::CacheManager::GetInstance().GetCheckIntervalMs();
            int sel = 1; // 30 sec default
            if (interval <= 10000) sel = 0;
            else if (interval <= 30000) sel = 1;
            else if (interval <= 60000) sel = 2;
            else if (interval <= 300000) sel = 3;
            else sel = 4;
            SendMessage(hCombo, CB_SETCURSEL, sel, 0);
        }
        UpdateCacheStatus();
    }
    else if (uMsg == WM_COMMAND)
    {
        WORD ctrlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (ctrlId == IDC_CFG_CACHE_CLEAR_BTN && notifyCode == BN_CLICKED)
        {
            SharePointCache::CacheManager::GetInstance().ClearDiskCache();
            SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CACHE_CLEARED), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        else if (ctrlId == IDC_CFG_CACHE_INTERVAL_COMBO && notifyCode == CBN_SELCHANGE)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_CFG_CACHE_INTERVAL_COMBO);
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            DWORD interval = 30000;
            switch (sel)
            {
            case 0: interval = 10000; break;
            case 1: interval = 30000; break;
            case 2: interval = 60000; break;
            case 3: interval = 300000; break;
            case 4: interval = 0xFFFFFFFF; break;
            }
            SharePointCache::CacheManager::GetInstance().SetCheckIntervalMs(interval);
            return TRUE;
        }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// CConfigDialog
//

class CCenteredPropertyWindow : public CWindow
{
public:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) != WA_INACTIVE && HWindow != NULL)
            {
                HWND hParent = GetParent(HWindow);
                if (hParent != NULL)
                    SalamanderGeneral->MultiMonCenterWindow(HWindow, hParent, TRUE);
            }
            break;
        }
        case WM_NCDESTROY:
        {
            LRESULT res = CWindow::WindowProc(uMsg, wParam, lParam);
            DetachWindow();
            delete this;
            return res;
        }
        }
        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

static int CALLBACK CenterCallback(HWND HWindow, UINT uMsg, LPARAM lParam)
{
    if (uMsg == PSCB_INITIALIZED)
    {
        CCenteredPropertyWindow* wnd = new CCenteredPropertyWindow;
        if (wnd != NULL)
        {
            wnd->AttachToWindow(HWindow);
            if (wnd->HWindow == NULL)
                delete wnd;
        }

        SharePointDarkMode::ApplyWindowTheme(HWindow);
    }
    return 0;
}

static DWORD LastCfgPage = 0;

CConfigDialog::CConfigDialog(HWND parent)
    : CPropertyDialog(parent, HLanguage, LoadStr(IDS_CFG_TITLE),
                      LastCfgPage, PSH_USECALLBACK | PSH_NOAPPLYNOW | PSH_HASHELP,
                      NULL, &LastCfgPage, CenterCallback)
{
    Add(&PageAccounts);
    Add(&PageDisplay);
    Add(&PageCache);
}

//
// CTransferProgressDialog
//

CTransferProgressDialog::CTransferProgressDialog(HWND hParent, bool isDownload, const std::string& fileName, int64_t totalBytes)
    : CCommonDialog(HLanguage, IDD_TRANSFER_PROGRESS, hParent),
      m_isDownload(isDownload),
      m_fileName(fileName),
      m_currentFileTotalBytes(totalBytes),
      m_currentFileBytesTransferred(0),
      m_totalBatchFiles(1),
      m_totalBatchBytes(totalBytes),
      m_completedBatchFiles(0),
      m_completedBatchBytes(0),
      m_cancelled(false),
      m_startTick(0),
      m_lastUpdateTick(0)
{
}

CTransferProgressDialog::~CTransferProgressDialog()
{
    Stop();
}

std::string CTransferProgressDialog::FormatSize(int64_t bytes)
{
    char buf[64];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else if (bytes < 1024 * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(buf, sizeof(buf), "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

bool CTransferProgressDialog::Start()
{
    if (!Create()) return false;

    HWND hParent = Parent;
    if (hParent) EnableWindow(hParent, FALSE);

    ShowWindow(HWindow, SW_SHOW);
    UpdateWindow(HWindow);

    m_startTick = GetTickCount();
    m_lastUpdateTick = m_startTick;
    return true;
}

void CTransferProgressDialog::Stop()
{
    if (HWindow)
    {
        HWND hParent = Parent;
        if (hParent)
        {
            EnableWindow(hParent, TRUE);
            SetForegroundWindow(hParent);
            SetFocus(hParent);
        }
        DestroyWindow(HWindow);
        HWindow = NULL;
    }
}

void CTransferProgressDialog::SetTotalBatch(int totalFiles, int64_t totalBytes)
{
    m_totalBatchFiles = totalFiles;
    m_totalBatchBytes = totalBytes;
}

void CTransferProgressDialog::SetCurrentFile(const std::string& fileName, int64_t totalBytes)
{
    m_fileName = fileName;
    m_currentFileTotalBytes = totalBytes;
    m_currentFileBytesTransferred = 0;

    if (HWindow)
    {
        SetDlgItemTextA(HWindow, IDC_TRANSFER_FILENAME, fileName.c_str());
        SendDlgItemMessage(HWindow, IDC_TRANSFER_PROGRESSBAR, PBM_SETPOS, 0, 0);
    }
}

void CTransferProgressDialog::OnFileCompleted(int64_t fileBytes)
{
    m_completedBatchFiles++;
    if (fileBytes > 0) m_completedBatchBytes += fileBytes;
}

void CTransferProgressDialog::ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
        {
            m_cancelled = true;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

bool CTransferProgressDialog::OnProgress(int64_t bytesTransferred, int64_t totalBytes)
{
    m_currentFileBytesTransferred = bytesTransferred;
    if (totalBytes > 0) m_currentFileTotalBytes = totalBytes;

    DWORD now = GetTickCount();
    if (now - m_lastUpdateTick >= 100) // Update UI at most 10 times/sec
    {
        m_lastUpdateTick = now;
        UpdateUI(bytesTransferred, totalBytes);
    }

    ProcessMessages();
    return !m_cancelled;
}

void CTransferProgressDialog::UpdateUI(int64_t bytesTransferred, int64_t totalBytes)
{
    if (!HWindow) return;

    if (totalBytes > 0)
    {
        int pct = (int)((bytesTransferred * 100) / totalBytes);
        SendDlgItemMessage(HWindow, IDC_TRANSFER_PROGRESSBAR, PBM_SETPOS, pct, 0);

        char szBytes[128];
        snprintf(szBytes, sizeof(szBytes), LoadStr(IDS_TRANSFER_BYTES_FMT),
                 FormatSize(bytesTransferred).c_str(), FormatSize(totalBytes).c_str());
        SetDlgItemTextA(HWindow, IDC_TRANSFER_BYTES, szBytes);
    }

    DWORD elapsed = GetTickCount() - m_startTick;
    if (elapsed > 500 && bytesTransferred > 0)
    {
        double speed = (double)bytesTransferred / ((double)elapsed / 1000.0);
        char szSpeed[128];
        snprintf(szSpeed, sizeof(szSpeed), LoadStr(IDS_TRANSFER_SPEED_FMT), FormatSize((int64_t)speed).c_str());
        SetDlgItemTextA(HWindow, IDC_TRANSFER_SPEED, szSpeed);
    }
}

INT_PTR CTransferProgressDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND)
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            m_cancelled = true;
            return TRUE;
        }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

void OnConfiguration(HWND hParent)
{
    CConfigDialog dlg(hParent);
    dlg.Execute();
}

void OnAbout(HWND hParent)
{
    SalamanderGeneral->SalMessageBox(hParent,
                                     "Microsoft SharePoint & OneDrive Plugin for Open Salamander\n"
                                     "Version 0.1.0\n"
                                     "Copyright \xA9 2026 fila73. Inspired by Red Salamander.\n"
                                     "Licensed under GPL-2.0-or-later.",
                                     LoadStr(IDS_ABOUT), MB_OK | MB_ICONINFORMATION);
}

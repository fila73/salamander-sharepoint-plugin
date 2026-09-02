// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CCommonDialog : public CDialog
{
public:
    CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin = ooStandard);
    CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin = ooStandard);

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void NotifDlgJustCreated();
};

class CCommonPropSheetPage : public CPropSheetPage
{
public:
    CCommonPropSheetPage(TCHAR* title, HINSTANCE modul, int resID,
                         DWORD flags = 0, HICON icon = NULL,
                         CObjectOrigin origin = ooStatic)
        : CPropSheetPage(title, modul, resID, flags, icon, origin) {}
    CCommonPropSheetPage(TCHAR* title, HINSTANCE modul, int resID, UINT helpID,
                         DWORD flags = 0, HICON icon = NULL,
                         CObjectOrigin origin = ooStatic)
        : CPropSheetPage(title, modul, resID, helpID, flags, icon, origin) {}

protected:
    virtual void NotifDlgJustCreated();
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CConfigPageAccounts : public CCommonPropSheetPage
{
public:
    CConfigPageAccounts();

    virtual void Transfer(CTransferInfo& ti) override;
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void RefreshAccountsList();
};

typedef CConfigPageAccounts CConfigPageGeneral;

class CConfigPageDisplay : public CCommonPropSheetPage
{
public:
    CConfigPageDisplay();

    virtual void Transfer(CTransferInfo& ti) override;
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};

class CConfigPageCache : public CCommonPropSheetPage
{
public:
    CConfigPageCache();

    virtual void Transfer(CTransferInfo& ti) override;
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void UpdateCacheStatus();
};

class CConfigDialog : public CPropertyDialog
{
protected:
    CConfigPageAccounts PageAccounts;
    CConfigPageDisplay PageDisplay;
    CConfigPageCache PageCache;

public:
    CConfigDialog(HWND parent);
};

class CTransferProgressDialog
{
public:
    CTransferProgressDialog(HWND hParent, bool isDownload, const std::string& fileName = "", int64_t totalBytes = 0);
    ~CTransferProgressDialog();

    bool Start();
    void Stop();
    void SetTotalBatch(int totalFiles, int64_t totalBytes = 0);
    void SetCurrentFile(const std::string& fileName, int64_t totalBytes);
    void OnFileCompleted(int64_t fileBytes = -1);

    bool OnProgress(int64_t bytesTransferred, int64_t totalBytes);
    bool IsCancelled() const { return m_cancelled; }
    void Cancel() { m_cancelled = true; }

    HWND GetHWND() const { return m_hDlg; }

    static std::string FormatSize(int64_t bytes);

private:
    static INT_PTR CALLBACK s_DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hParent;
    HWND m_hDlg;
    bool m_isDownload;
    std::string m_fileName;
    int64_t m_currentFileTotalBytes;
    int64_t m_currentFileBytesTransferred;
    int m_totalBatchFiles;
    int64_t m_totalBatchBytes;
    int m_completedBatchFiles;
    int64_t m_completedBatchBytes;
    bool m_cancelled;
    DWORD m_startTick;
    DWORD m_lastUpdateTick;

    void ProcessMessages();
    void UpdateUI(int64_t bytesTransferred, int64_t totalBytes);
};

class CDialogTransferProgress
{
public:
    static std::shared_ptr<CDialogTransferProgress> Create(HWND hParent, bool isDownload)
    {
        return std::make_shared<CDialogTransferProgress>(hParent, isDownload);
    }

    CDialogTransferProgress(HWND hParent, bool isDownload)
        : m_dlg(std::make_unique<CTransferProgressDialog>(hParent, isDownload)),
          m_cancelFlag(false)
    {
        m_dlg->Start();
    }

    ~CDialogTransferProgress()
    {
        Close();
    }

    void SetTotalItemsCount(int count)
    {
        m_totalItems = count;
        if (m_dlg) m_dlg->SetTotalBatch(count, m_totalBytes);
    }

    void SetTotalBatchBytes(int64_t bytes)
    {
        m_totalBytes = bytes;
        if (m_dlg) m_dlg->SetTotalBatch(m_totalItems, bytes);
    }

    void SetCurrentFileName(const std::string& name)
    {
        if (m_dlg) m_dlg->SetCurrentFile(name, 0);
    }

    void UpdateFileProgress(int64_t bytes, int64_t total)
    {
        if (m_dlg)
        {
            m_dlg->OnProgress(bytes, total);
            if (m_dlg->IsCancelled()) m_cancelFlag = true;
        }
    }

    void UpdateBatchProgress(int64_t batchBytes, int64_t totalBatchBytes)
    {
    }

    void IncrementCompletedItems()
    {
        if (m_dlg) m_dlg->OnFileCompleted();
    }

    bool IsCancelled() const
    {
        return m_cancelFlag || (m_dlg && m_dlg->IsCancelled());
    }

    const bool* GetCancelFlagPtr() const
    {
        return &m_cancelFlag;
    }

    void Close()
    {
        if (m_dlg)
        {
            m_dlg->Stop();
            m_dlg.reset();
        }
    }

private:
    std::unique_ptr<CTransferProgressDialog> m_dlg;
    bool m_cancelFlag = false;
    int m_totalItems = 0;
    int64_t m_totalBytes = 0;
};

void OnConfiguration(HWND hParent);
void OnAbout(HWND hParent);

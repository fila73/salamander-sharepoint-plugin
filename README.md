# Microsoft SharePoint Plugin for Open Salamander

A full-featured filesystem, management, and viewer plugin for **Open Salamander** (and the Dark Mode fork **KRtkovo-eu-AI/salamander**), allowing you to browse, manage, upload, download, and organize files and folders on **Microsoft SharePoint** and **OneDrive for Business** directly within Salamander's dual-pane interface with zero-latency caching.

---

## ✨ Features

### 🚀 Smart In-Memory & Persistent Disk Metadata Cache
- **Instant 0 ms Browsing**: Folder contents are cached in RAM and persisted across Salamander restarts to `%APPDATA%\Open Salamander\plugins\sharepoint\cache_<account_hash>.bin`.
- **Microsoft Graph Delta API Synchronization**:
  - Automatically queries incremental changes using lightweight delta tokens.
  - Selectively invalidates only modified folders instead of re-downloading whole trees.
  - Zero unnecessary network requests if nothing changed on remote storage.

### 👥 Multi-Account & Tenant Support
- Sign in to **multiple Microsoft 365 accounts** simultaneously (e.g. personal OneDrive, corporate SharePoint tenants).
- **Tenant ID Flexibility**: Connect to multi-tenant (`common`), organizational (`organizations`), or specific private Azure AD / Entra ID tenant IDs.
- **100% Data Isolation**: Each account maintains its own isolated disk cache file.
- **Encrypted Storage (Windows DPAPI)**: Tokens are securely protected with Windows DPAPI per user profile.
- **Silent Background Refresh**: Access tokens renew automatically without browser popups.

### 🏢 Comprehensive SharePoint & OneDrive Hierarchy
- **`OneDrive`**: Instant access to personal / work cloud files (`/v1.0/me/drive/root`).
- **`Sites`**: Seamless browsing across SharePoint Sites (`/v1.0/sites`) and Document Libraries (`/v1.0/sites/{id}/drives`).
- **`Shared with me`**: Files shared directly with you.
- **`Recent`**: Recently accessed and modified cloud documents.
- **`Recycle Bin`**: Cloud trash with restore and permanent delete options.

### ✏️ Complete File Management & Write Operations
- 📁 **F7 – Create Folder (`CreateDir`)**: Create new folders in OneDrive, SharePoint Document Libraries, and subfolders.
- ✏️ **Shift+F6 – Quick Rename (`QuickRename`)**: Rename files and folders directly in the panel with immediate refresh.
- 🗑️ **F8 – Delete & Recycle Bin (`Delete`)**: Move items to SharePoint / OneDrive Recycle Bin.
- 📤 **F5 – Upload Files & Folders**:
  - Simple direct upload (<4MB) and high-throughput chunked Upload Sessions for large files (>4MB).
  - **Pass 1 Pre-scan & Dual Progress Bars**: Calculates exact item count and byte totals to display both current file and total batch progress.
  - **Directory & File Collision Resolution**: Interactive prompt when folders or files already exist (Merge / Overwrite, Keep Both, Skip, Apply to all).
- 📥 **F5 – Download to Local Disk**: Fast streaming download with live progress dialog.
- 👁️ **F3 – Internal Viewer**: Instant preview of remote files with automatic temp caching.
- 📊 **Storage Quota & Free Space Indicator**: Displays remaining and total storage space in Salamander's panel footer.
- 📋 **Custom Detailed Panel Columns (`CSalamanderViewAbstract`)**:
  - **`Author`**: Displays the creator name / email.
  - **`Modified By`**: Shows the user who last modified the item.
  - **`Shared`**: Indicates whether an item is shared.
  - **`Web URL`**: Direct link to SharePoint Online / Office Web App.
- 🔎 **Cloud Search (`Alt+F7`)**:
  - Fast search powered by Microsoft Graph Search API.
  - Scope selection: current site, current library, entire OneDrive / SharePoint.
  - Focus & jump to item in panel on double-click.

### ⭐ Context Menu & Tools
- *Open in Web Browser* (SharePoint / Office for the Web)
- *Copy Link to Clipboard*
- *Calculate Folder Size...* (`Alt+F10` / `Space` in panel)

### 🌙 Dark Mode Support
- Fully integrated dark theme matching Open Salamander's native dark mode palette.

---

## 🚀 Installation

1. Download the latest release `salamander-sharepoint-plugin.zip`.
2. Extract files into the `plugins\sharepoint` directory of your Open Salamander installation:
   ```
   Open Salamander/
   └── plugins/
       └── sharepoint/
           ├── sharepoint.spl
           └── lang/
               ├── english.slg
               └── czech.slg
   ```
3. Open **Open Salamander**, navigate to **Plugins → Plugins Manager...**, click **Add...**, and select `sharepoint.spl`.
4. In the panel, press **Alt+F1** or **Alt+F2** and choose the **sharepoint:** drive.
5. In **Plugins → Microsoft SharePoint → Configuration...**, add your Microsoft account.

---

## ⚙️ Microsoft Azure AD / Entra ID App Registration

To connect to your SharePoint and OneDrive storage:
1. Go to [Microsoft Entra admin center / Azure Portal](https://portal.azure.com/).
2. Navigate to **App registrations → New registration**.
3. Set Supported account types to **Accounts in any organizational directory and personal Microsoft accounts** (or single tenant if restricted).
4. In **Authentication**, add a **Mobile and desktop applications** platform with redirect URI: `http://localhost`.
5. Under **API permissions**, add delegated permissions for **Microsoft Graph**:
   - `offline_access`
   - `Files.ReadWrite.All`
   - `Sites.ReadWrite.All`
   - `User.Read`
6. Copy the **Application (client) ID** and optionally your **Directory (tenant) ID**.
7. Enter them in the plugin configuration dialog and click **Add Account...**.

---

## 🛠️ Compilation from Source (MinGW-w64)

### Requirements:
- MinGW-w64 (GCC with C++17 support)
- `mingw32-make`
- Open Salamander SDK (in `../salamander-plugins/salamand`)

### Build Command:
```powershell
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw
```

Outputs:
- `sharepoint.spl` (Plugin binary)
- `english.slg` (English language resources)
- `czech.slg` (Czech language resources)

---

## 📄 License & Authors

Licensed under the **GNU General Public License v2.0 or later** ([GPL-2.0-or-later](LICENSE)).

- Author: **fila73**
- Inspired by **Red Salamander**
- Dark Mode integration based on fork by **Ondrej Kotas (KRtkovo-eu-AI)**.

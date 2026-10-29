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

## ⚙️ Authentication & Microsoft Account Setup (Azure Entra ID)

The plugin supports both **Personal Microsoft Accounts (OneDrive)** and **Work/School Microsoft 365 Accounts (SharePoint Online & OneDrive for Business)**.

### 🔑 Selecting Tenant ID in Plugin Configuration:
In the configuration dialog (*Plugins → Microsoft SharePoint → Configuration...*), set the Tenant ID according to your account type:
- **`consumers`** – **For Personal Accounts (@outlook.com, @hotmail.com, @live.com, personal OneDrive)**:
  - Does **NOT** require any paid SharePoint license. Accesses your personal OneDrive directly.
- **`organizations`** – For any Microsoft 365 Work or School account.
- **`common`** – Universal mode allowing sign-in with either personal or organizational accounts.
- **`<specific-tenant-guid>`** – Specific Azure AD / Entra ID tenant ID of your organization.

---

### 📋 Custom Azure Entra ID App Registration (Client ID):

If configuring your own `Client ID`:
1. Go to [Microsoft Entra admin center / Azure Portal](https://portal.azure.com/).
2. Navigate to **App registrations → New registration**.
3. Set **Supported account types** to **Accounts in any organizational directory and personal Microsoft accounts** *(Multitenant + Personal)*.
4. In **Authentication**:
   - Add platform **Mobile and desktop applications**.
   - Check redirect URI: **`http://localhost`**.
   - Set **Allow public client flows** to **Yes**.
5. Under **API permissions**, add delegated permissions for **Microsoft Graph**:
   - `offline_access`
   - `Files.ReadWrite.All`
   - `Sites.ReadWrite.All`
   - `User.Read`
6. Copy the **Application (client) ID**.
7. Enter it in the plugin configuration dialog, choose the corresponding **Tenant ID** (`consumers` for personal, `common` or `organizations` for work), and click **Add Account...**.

> [!NOTE]
> **"Tenant does not have a SPO license" Error:**
> If you sign in with an organizational account in a tenant without an active **SharePoint Online (SPO)** license (e.g. a free Azure AD tenant without Microsoft 365 plans), Microsoft Graph will reject OneDrive and Sites operations. For free personal usage without a SharePoint license, set Tenant ID to `consumers`.

---

## 🛠️ Compilation from Source (MSVC / MSBuild)

### Requirements:
- Visual Studio 2022+ (MSVC, C++17) with MSBuild
- Open Salamander plugin SDK (default `..\..\samandarin\src` relative to the plugin; override with `/p:SalamanderSdkDir=<path>`)

### Build Command:
```powershell
MSBuild src\vcxproj\sharepoint.vcxproj /p:Configuration=Release /p:Platform=x64
```

Outputs:
- `build\Release_x64\sharepoint.spl` (Plugin binary)
- `build\Release_x64\lang\english.slg` (English language resources)
- `build\Release_x64\lang\czech.slg` (Czech language resources)

---

## 📄 License & Authors

Licensed under the **GNU General Public License v2.0 or later** ([GPL-2.0-or-later](LICENSE)).

- Author: **fila73**
- Inspired by **Red Salamander**
- Dark Mode integration based on fork by **Ondrej Kotas (KRtkovo-eu-AI)**.

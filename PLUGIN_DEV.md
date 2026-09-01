# Návod na vývoj pluginu Microsoft SharePoint pro Open Salamander (Template & Best Practices)

Tento dokument slouží jako kompletní vzor a vývojová kuchařka pro tvorbu a údržbu pluginu **Microsoft SharePoint / OneDrive for Business** pro **Open Salamander** (včetně podpory Dark Mode forku **KRtkovo-eu-AI/salamander**), postavený na ověřených postupech z vývoje pluginů `salamander-gdrive-plugin` a `salamander-sqlite-plugin`.

---

## 1. Architektura a adresářová struktura pluginu

Plugin do Salamandera se skládá z:
- **`sharepoint.spl`** – Samotná dynamická knihovna pluginu (Windows DLL přejmenovaná na `.spl`).
- **`lang/*.slg`** – Jazykové knihovny s přeloženými texty a dialogy (`english.slg`, `czech.slg`).

### Adresářová struktura projektu:

```
salamander-sharepoint-plugin/
├── .gitignore
├── LICENSE                         # GPL-2.0-or-later + SPDX hlavičky
├── Makefile.mingw                  # Sestavení pomocí MinGW-w64 (GCC/G++, windres)
├── PLUGIN_DEV.md                   # Vývojová kuchařka & best practices (tento soubor)
├── README.md                       # Uživatelská dokumentace v angličtině
├── README_CZ.md                    # Uživatelská dokumentace v češtině
└── src/
    ├── sharepoint.cpp              # Hlavní vstup, DllMain, CPluginInterface
    ├── sharepoint.h                # Hlavičkový soubor pluginu
    ├── sharepoint.def              # Exportní definice (SalamanderPluginEntry2)
    ├── sharepoint.rc               # Hlavní resources (verze, ikony)
    ├── sharepoint.rh2              # Definice číselných ID resource a příkazů
    ├── versinfo.rh2                # Hlavička pro verzi a metadata pluginu
    ├── precomp.h                   # Precompiled headers se Salamander SDK
    ├── precomp.cpp
    ├── sharepointdarkmode.h        # Implementace Dark Mode podpory
    ├── sharepointdarkmode.cpp
    ├── sharepoint_http.h           # WinHTTP klient (SSL/TLS, chunked upload/download, timeouty)
    ├── sharepoint_http.cpp
    ├── sharepoint_json.h           # Lehký JSON parser / serializer
    ├── sharepoint_json.cpp
    ├── sharepoint_auth.h           # OAuth 2.0 PKCE, lokální HTTP listener, DPAPI šifrování, Multi-Account
    ├── sharepoint_auth.cpp
    ├── sharepoint_api.h            # Klient pro Microsoft Graph REST API v1.0
    ├── sharepoint_api.cpp
    ├── sharepoint_cache.h          # Paměťový strom + disková mezipaměť s Graph Delta Sync
    ├── sharepoint_cache.cpp
    ├── sharepoint_fs.h             # CPluginFSInterfaceAbstract – definice souborového systému
    ├── sharepoint_fs.cpp           # Životní cyklus FS, kvóta a diskový prostor
    ├── sharepoint_fs_nav.cpp       # Navigace stromy: Sites -> Drives -> Folders -> Files
    ├── sharepoint_fs_transfer.cpp  # F5 Upload/Download (jednoduchý + chunked upload session >4MB)
    ├── sharepoint_fs_ops.cpp       # F7 CreateDir, Shift+F6 QuickRename, F8 Delete (Recycle Bin)
    ├── sharepoint_log.h            # Diagnostické logování
    ├── sharepoint_log.cpp
    ├── dialogs.h                   # Správce účtů, konfigurace, kolizní dialogy
    ├── dialogs.cpp
    ├── dialog_find.h               # Alt+F7 Cloudové vyhledávání na SharePointu / OneDrive
    ├── dialog_find.cpp
    ├── menu.cpp                    # Kontextová a hlavní menu pluginu
    └── lang/
        ├── lang.rh                 # Společné resource ID pro jazyky
        ├── lang_en.rc              # Anglické texty a dialogy
        └── lang_cs.rc              # České texty a dialogy
```

---

## 2. Licencování a hlavičky souborů (SPDX & Autorské záznamy)

Pluginy pro Open Salamander podléhají licenci **GPL-2.0-or-later** (v souladu s licencí jádra Open Salamander SDK). Pokud jsou části kódu přejímány nebo portovány z moderního forku RedSalamander (který používá licenci **MIT**), je kód plně kompatibilní a v souborech je nutné uvádět příslušné autory a SPDX identifikátory.

### 1. Formát hlaviček ve zdrojových souborech (`.cpp`, `.h`, `.rh`, `.rh2`, `.rc2`)

Každý vytvořený nebo upravovaný soubor musí na prvních řádcích obsahovat standardizovanou SPDX hlavičku:

#### Běžné zdrojové soubory pluginu:
```cpp
// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later
```

#### Soubory obsahující integraci Dark Mode (`*darkmode.cpp`, `*darkmode.h`, `versinfo.rh2`):
```cpp
// SPDX-FileCopyrightText: 2026 fila73 & Ondrej Kotas
// SPDX-FileContributor: Dark Mode host policy and theme integration based on fork by Ondrej Kotas (KRtkovo-eu-AI)
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later
```

### 2. Struktura souboru `LICENSE` v kořenu repozitáře
Soubor `LICENSE` obsahuje text **GNU General Public License v2.0**:

```text
GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

Copyright (C) 1989, 1991 Free Software Foundation, Inc.
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

Everyone is permitted to copy and distribute verbatim copies
of this license document, but changing it is not allowed.

Copyright (C) 2026 fila73. Inspired by Red Salamander.
This plugin is compatible with Open Salamander and is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later).
```

---

## 3. Klíčové rozhraní Open Salamander Plugin SDK

Plugin komunikuje se Salamanderem přes virtuální C++ třídy definované v `salamander-plugins/salamand/plugins/shared/`:

### Hlavní rozhraní:
1. **`CPluginInterface`**:
   - `Init()` – Inicializace pluginu po načtení.
   - `Connect(HWND parent, CSalamanderConnectAbstract* salamander)` – Registrace schopností pluginu (např. `salamander->AddFileSystem(...)`, `salamander->AddIcon(...)`).
   - `LoadConfiguration(CSalamanderRegistryAbstract* registry, HKEY regKey)` – Načtení konfigurace z registru.
   - `SaveConfiguration(CSalamanderRegistryAbstract* registry, HKEY regKey)` – Uložení konfigurace.
   - `About(HWND parent)` a `Configuration(HWND parent)` – Dialogy O aplikaci a Nastavení.

2. **`CPluginInterfaceForFS`** / **`CPluginFSInterfaceAbstract`** (souborový systém):
   - Procházení adresářů (`OpenDirectory`, `ReadDirectory`, `CloseDirectory`).
   - Práce se soubory (`GetFileInfo`, `CopyOrMoveFromDiskToFS`, `CopyOrMoveFromFS`, `Delete`, `CreateDir`, `QuickRename`).
   - Volný prostor na disku / kvóta (`GetFSFreeSpace`).

3. **`CSalamanderViewAbstract`** (vlastní sloupce panelu):
   - Sloupce: *Vytvořil*, *Změnil*, *Sdíleno*, *Web URL*.
   - Podpora řazení a nastavitelné šířky sloupců.

---

## 4. Architektura Microsoft Graph API a SharePointu

### 1. Hierarchie v SharePoint pluginu
SharePoint a OneDrive organizují data do hierarchie:
- **Kořen (`sharepoint:\`)**:
  - `OneDrive` – Osobní / firemní disk uživatele (`/v1.0/me/drive/root`).
  - `Sites` – Přehled SharePoint webů (`/v1.0/sites?search=*` a `/v1.0/me/followedSites`).
  - `Shared with me` – Soubory sdílené s přihlášeným uživatelem (`/v1.0/me/drive/sharedWithMe`).
  - `Recent` – Nedávno otevřené a upravované soubory (`/v1.0/me/drive/recent`).
  - `Recycle Bin` – Koš (`/v1.0/me/drive/recycleBin` nebo koš webu).
- **Uvnitř webu (`sharepoint:\Sites\<SiteName>\`)**:
  - Knihovny dokumentů (Document Libraries / Drives) daného webu (`/v1.0/sites/{site-id}/drives`).
- **Uvnitř knihovny dokumentů (`sharepoint:\Sites\<SiteName>\<DocLib>\<Folders...>\`)**:
  - Běžné soubory a složky (DriveItems: `/v1.0/drives/{drive-id}/items/{item-id}/children`).

### 2. Autentizace (Microsoft Identity Platform OAuth 2.0 PKCE)
- **Autorizační URL**: `https://login.microsoftonline.com/common/oauth2/v2.0/authorize` (případně specifické `Tenant ID` pro firemní účty).
- **Token URL**: `https://login.microsoftonline.com/common/oauth2/v2.0/token`.
- **Scopes**: `offline_access Files.ReadWrite.All Sites.ReadWrite.All User.Read`.
- **PKCE**: Generování kryptografického code verifieru a SHA-256 code challenge.
- **Lokální loopback listener**: HTTP listener na `127.0.0.1:<port>` pro zachycení autorizačního kódu z webového prohlížeče.
- **Bezpečné úložiště**: Ukládání refresh tokenů šifrovaných pomocí Windows DPAPI (`CryptProtectData`).

### 3. Přenosy velkých souborů (> 4 MB)
- Pro malé soubory (< 4 MB): Jednoduchý `PUT /v1.0/drives/{drive-id}/items/{parent-id}:/{filename}:/content`.
- Pro velké soubory (>= 4 MB): Vytvoření relace nahrávání `POST /v1.0/drives/{drive-id}/items/{parent-id}:/{filename}:/createUploadSession` a následný přenos po blocích (např. 1–8 MB) s hlavičkou `Content-Range: bytes START-END/TOTAL`.

### 4. Graph Delta Sync (Okamžitá mezipaměť & minimální latence)
- Pro každou knihovnu dokumentů se udržuje `@odata.deltaLink`.
- Periodická synchronizace zjišťuje pouze upravené/smazané položky bez nutnosti stahovat celou strukturu složek.
- Disková mezipaměť se ukládá do `%APPDATA%\Open Salamander\plugins\sharepoint\cache_<account_hash>.bin`.

---

## 5. Zásadní pravidla a řešení známých chyb (Gotchas)

### ⚠️ 1. Statické linkování běhových knihoven v MinGW (`-static`)
Vždy použijte `-static -static-libgcc -static-libstdc++`. Jinak bude Salamander vyžadovat externí MinGW runtime DLL (`Chyba 126`).

### ⚠️ 2. Ošetření prázdného klíče registru při prvním spuštění (Chyba 6)
Při prvním spuštění předává Salamander `regKey == NULL`. V `LoadConfiguration` je nutné ověřit `if (registry != NULL && regKey != NULL)`.

### ⚠️ 3. Životní cyklus `QuickRename` a `CreateDir`
- `mode == 1`: Vrátit `FALSE` a `cancel = FALSE` pro standardní inline editaci v panelu Salamandera.
- `mode == 2`: Provést operaci a po dokončení zavolat:
  ```cpp
  SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE);
  SalamanderGeneral->PostRefreshPanelFS(this);
  return TRUE;
  ```

### ⚠️ 4. Storno operace (Cancel) v `CopyOrMoveFromDiskToFS`
Při stisknutí Storno uživatelem nastavit `*cancelOrHandlePath = TRUE` a vrátit `TRUE`, aby Salamander nezobrazoval chybový dialog znovu.

### ⚠️ 5. Disambiguace duplicitních názvů v jedné složce
Cloudová úložiště mohou mít stejnojmenné položky. Panel Salamandera vyžaduje unikátní názvy. Plugin automaticky připojuje suffix s ID (např. `Dokument [a8F2k1].docx`).

---

## 6. Sestavení ze zdrojových kódů (MinGW-w64)

```powershell
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw
```

Příkaz vytvoří:
- `sharepoint.spl` (Dynamická knihovna pluginu)
- `english.slg` (Anglické jazykové zdroje)
- `czech.slg` (České jazykové zdroje)

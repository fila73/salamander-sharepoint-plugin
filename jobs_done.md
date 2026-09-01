# Jobs Done - Open Salamander Microsoft SharePoint Plugin

Chronologický přehled dokončených prací, implementačních kroků a vyřešených problémů.

---

## 2026-09-01

### 1. Inicializace projektu a architektury
- Vytvoření projektové struktury podle vzoru `salamander-gdrive-plugin` a Open Salamander SDK (`salamander-plugins`).
- Nastavení MinGW-w64 build systému (`Makefile.mingw`) s podporou C++20, statického linkování runtime (`-static -static-libgcc -static-libstdc++`) a potřebných knihoven (`-lwinhttp`, `-lcrypt32`, `-lws2_32`, `-lshlwapi`, `-lcomctl32`, atd.).
- Příprava jazykových zdrojů (angličtina `english.slg`, čeština `czech.slg`) a resource skriptů.

### 2. Implementace Microsoft Graph API a autentizace (OAuth 2.0 PKCE)
- Implementace `SharePointAuth::AuthManager`:
  - Lokální HTTP loopback server pro zachycení autorizačního kódu.
  - Podpora PKCE (SHA-256 code challenge/verifier).
  - Bezpečné ukládání a šifrování tokenů pomocí Windows DPAPI do Windows registru.
  - Automatická obnova expirovaných access tokenů pomocí refresh tokenu.
  - Možnost manuálního zadání vlastního Client ID / Tenant ID v konfiguraci pluginu.
- Implementace `SharePointApi::ApiClient`:
  - Listování kořenových kategorií: `OneDrive`, `Sites` (SharePoint weby), `Shared with me`, `Recent`, `Recycle Bin`.
  - Listování dokumentových knihoven a podsložek.
  - Download a upload souborů (včetně chunked upload session pro soubory > 4 MB).
  - Vytváření složek, mazání, přejmenování, kopírování a přesouvání položek.

### 3. Oprava validace jazykových modulů (.slg)
- **Problém**: Salamander odmítal načíst `english.slg` s chybou *"Plugin language file was not found or is not valid"*.
- **Příčina**: Salamander (`IsSLGFileValid` v `salamdr2.cpp`) ověřuje přítomnost a shodu verzovacích informací `VS_VERSION_INFO` (`dwFileVersionMS` / `dwFileVersionLS`) mezi `.spl` a `.slg`.
- **Řešení**:
  - Vytvořeny resource hlavičky `lang_en.rc2` a `lang_cs.rc2` s definicí `_LANG` a inkluzí `versinfo.rc2`.
  - `.slg` soubory nyní obsahují validní `RT_VERSION` resource.

### 4. Oprava loopback listeneru pro Microsoft Entra ID
- **Problém**: Chyba `AADSTS50011` při přihlašování v Azure AD.
- **Příčina**: Azure AD desktop platforma vyžaduje `http://localhost` bez podcesty (`/callback`).
- **Řešení**: Změněno na dynamický port `http://localhost:<port>` a parser na lokálním socketu upraven pro `GET /?code=...`.

### 5. Oprava síťové komunikace (IPv6 `::1` a buffery)
- **Problém**: V prohlížeči se po přihlášení zobrazila chyba `ERR_SOCKET_NOT_CONNECTED`.
- **Příčiny**:
  1. Velikost autorizačního kódu Microsoft Entra přesahuje 1 700 znaků (celý požadavek má 4–6 KB).
  2. Windows 10/11 a moderní prohlížeče překládají `localhost` primárně na **IPv6 `::1`** namísto IPv4 `127.0.0.1`.
- **Řešení**:
  - Implementován duální listener naslouchající na stejném portu jak pro **IPv4** (`127.0.0.1`), tak pro **IPv6** (`[::1]`).
  - Klientský socket přepnut do blokujícího režimu s timeoutem a čtením ve smyčce až do konce HTTP hlaviček (`\r\n\r\n`) s kapacitou bufferu až 64 KB.
  - Zaveden korektní `shutdown(clientSock, SD_BOTH)` před uzavřením spojení.

### 6. Oprava pádu při vstupu do složky v panelu (Memory Allocation Fix)
- **Problém**: Pád Salamanderu při otevření složky (např. OneDrive).
- **Příčina**: Struktura `CFileData.Name` byla předávána jako ukazatel na dočasný C++ string (`item.name.c_str()`). Salamander při uvolňování/obnově panelu volá `SalamanderGeneral->Free(file.Name)` a očekává paměť alokovanou přes `SalamanderGeneral->DupStr` / `SalamanderGeneral->Alloc`.
- **Řešení**:
  - Vytvořena funkce `AddItemToDir()`, která správně alokuje `file.Name` přes `SalamanderGeneral->DupStr()`, nastavuje atributy, ikony (`pitFromRegistry`), flags `SALDIRFLAG_IGNOREDUPDIRS` a navigační položku `..`.

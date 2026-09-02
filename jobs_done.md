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

### 7. Oprava zachování kurzoru a stability navigace (`CutDirectory` a `ExecuteOnFS`)
- **Problém**: Při přechodu do nadřazeného adresáře (`..` nebo Backspace) skočil kurzor na první položku, případně došlo k chybě při nestandardním ořezávání cesty.
- **Příčina**:
  1. V `ExecuteOnFS` chyběl parametr `suggestedFocusName` u `ChangePanelPathToPluginFS`.
  2. Ruční manipulace se stringy cest (`substr`/`rfind`) nebyla plně kompatibilní s interními normami Salamanderu.
- **Řešení**:
  - `ExecuteOnFS` a `GetFullName` sjednoceny na standardní SDK funkci `SalamanderGeneral->CutDirectory()`, která zkracuje cestu a vrací ukazatel na odříznutou podsložku (`cutDir`).
  - Tento název se předává do `ChangePanelPathToPluginFS(panel, pluginFSName, newPath, NULL, -1, cutDir)` – Salamander na ni automaticky a stabilně nastaví kurzor.

---

## 2026-09-02

### 8. Oprava chyby 401 Unauthorized při stahování (F3 a F5)
- **Problém**: Prohlížení složek fungovalo, ale při F3 (zobrazení souboru) i F5 (kopírování na lokální disk) vracel server chybu `401 Unauthorized`.
- **Příčina**:
  1. `item.downloadUrl` (`@microsoft.graph.downloadUrl`) je předem podepsaná URL směřující na SharePoint CDN / storage server. Odeslání hlavičky `Authorization: Bearer <token>` na tento server vyústí v `401 Unauthorized` (storage server odmítne Graph Bearer token).
  2. Při volání Graph endpointu `/content` vrací Graph stav `302 Found` s přesměrováním na storage server. WinHTTP ve výchozím nastavení přeposílal Bearer token i po redirectu.
  3. V `CPluginFS::Transfer` a `CPluginFS::ViewFile` se vytvářel neúplný `SharePointItem` bez `downloadUrl` a `driveId`.
- **Řešení**:
  - Přidána metoda `CPluginFS::FindItemObjectByPanelName()`, která načítá kompletní položku včetně `driveId` a `downloadUrl`.
  - V `HttpClient::DownloadToFile` zavedeno rozlišení Graph API vs. přímá CDN URL – Bearer token se odesílá pouze na `graph.microsoft.com`.
  - Pro Graph endpointy nastaveno `WINHTTP_OPTION_REDIRECT_POLICY_NEVER` a při zachycení 302/301/303/307 se cíl stahuje z `Location:` bez autorizační hlavičky.
  - V `ApiClient::DownloadFile` se nejprve zkouší přímé rychlé stažení přes `item.downloadUrl` a teprve při neúspěchu/expiraci se volá Graph `/content` endpoint.

### 9. Oprava pádu aplikace po odkliknutí chyby při kopírování (F5)
- **Problém**: Pokud stahování selhalo (např. chyba 401), po odkliknutí chybové hlášky `SalMessageBox` došlo k pádu Salamanderu.
- **Příčina**:
  1. Modální progress dialog `CTransferProgressDialog::Start` zakázal okno rodiče (`EnableWindow(Parent, FALSE)`). `SalMessageBox` byl volán dříve, než byl progress dialog uzavřen a rodičovské okno opět povoleno.
  2. `CopyOrMoveFromFS` po zobrazení chyby vrátil `return TRUE;` s `cancelOrHandlePath = FALSE;`. Salamander to chápal tak, že soubor byl úspěšně zkopírován a pokusil se načíst atributy neexistujícího souboru na disku.
- **Řešení**:
  - Při selhání stahování se nejdříve zavolá `progressDlg->Close()`, čímž se rodičovské okno reaktivuje a získá fokus.
  - Nastaveno `cancelOrHandlePath = TRUE;` a vráceno `TRUE;`, takže Salamander ví, že operace skončila chybou/stornem a nepokouší se o dodatečné operace s neexistujícími soubory.

### 10. Oprava pádu po dokončení kopírování (Heap Corruption 0xC0000374 v CTransferProgressDialog)
- **Problém**: Po dokončení přenosu souborů došlo k pádu aplikace (`STATUS_HEAP_CORRUPTION 0xC0000374`).
- **Příčina**: Třída `CTransferProgressDialog` dědila z `CCommonDialog` / `CDialog` / `CWindowsObject` (`winliblt`). Tyto třídy Open Salamander frameworku interně spravují životní cyklus oken a paměti (`ooDynamic` / `ooStandard`), což při ručním řízení paměti a volání `delete` v destruktoru `CDialogTransferProgress::Close()` vedlo ke kolizi alokátorů a poškození haldy.
- **Řešení**: `CTransferProgressDialog` byl plně osamostatněn od tříd `CCommonDialog`/`CDialog` a přepsán na čisté nativní Win32 `CreateDialogParamA` / `DestroyWindow` s bezpečným předáním `this` přes `GWLP_USERDATA` (obdobně jako `CSharePointFindDialog`), což garantuje 100% stabilitu haldy.



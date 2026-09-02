# Implementační plán: Oprava chyby 401 Unauthorized při stahování (F3, F5) a pádu po odkliknutí chyby (F5)

## 1. Popis problému a příčiny

### A. Chyba 401 Unauthorized při stahování (F3 a F5)
1. **Pre-autentizované URL (`@microsoft.graph.downloadUrl`)**:
   - Microsoft Graph při procházení složek vrací přímou download URL adresu (`@microsoft.graph.downloadUrl`). Tato adresa směřuje na storage CDN servery OneDrive/SharePointu a v query parametrech již nese autorizační token.
   - Odeslání hlavičky `Authorization: Bearer <token>` na tyto CDN servery způsobí chybu **`401 Unauthorized`** (úložiště odmítá Bearer tokeny určené pro Graph API).
2. **Redirect 302 na Graph endpointu `/content`**:
   - Pokud `downloadUrl` není k dispozici, volá se `https://graph.microsoft.com/v1.0/.../items/{id}/content` s Bearer tokenem.
   - Graph API odpoví stavem `302 Found` a přesměruje na storage server.
   - WinHTTP ve výchozím nastavení přeposílá původní hlavičku `Authorization: Bearer <token>` i na cílový server po přesměrování, což vede ke stejné chybě `401 Unauthorized`.
3. **Ztráta metadat v `CopyOrMoveFromFS` a `ViewFile`**:
   - V `CPluginFS::Transfer` i `CPluginFS::ViewFile` se vytvářel prázdný objekt `SharePointItem`, kde chybělo `driveId` i `downloadUrl`.

### B. Pád pluginu / Salamanderu po odkliknutí chyby při F5
1. **Rodičovské okno zakázáno modalním progress dialogem**:
   - `CTransferProgressDialog::Start()` volá `EnableWindow(Parent, FALSE)`. Při chybě stahování se volal `SalMessageBox(parent, ...)` bez předchozího uzavření progress dialogu a reaktivace rodičovského okna.
2. **Nekorektní návratový stav `cancelOrHandlePath`**:
   - `CopyOrMoveFromFS` po zobrazení chyby vrátil `return TRUE;` s `cancelOrHandlePath = FALSE;`. Salamander to vyhodnotil tak, že soubor byl úspěšně stažen na disk a pokusil se pracovat s neexistujícím lokálním souborem, což vedlo k pádu.

---

## 2. Plánované kroky implementace

1. **`src/sharepoint_http.cpp` & `src/sharepoint_http.h`**:
   - V `HttpClient::DownloadToFile`:
     - Zjistit, zda je URL adresa směřována na `graph.microsoft.com`.
     - Pouze pro `graph.microsoft.com` připojit `Authorization: Bearer <token>` a nastavit `WINHTTP_OPTION_REDIRECT_POLICY_NEVER`.
     - Při záchytu stavu redirectu (`301`, `302`, `303`, `307`, `308`) přečíst hlavičku `Location:` a provést stažení z cílové adresy bez `Authorization` hlavičky.
     - Pro přímé download URL (mimo Graph API) neposílat `Authorization: Bearer`.
2. **`src/sharepoint_api.cpp`**:
   - V `ApiClient::DownloadFile`:
     - Nejprve zkusit stažení přes `item.downloadUrl` bez tokenu.
     - Pokud `item.downloadUrl` selže nebo není přítomna, dotázat se na `/content` endpoint s Graph tokenem (s automatickým zpracováním 302).
3. **`src/sharepoint_fs.h` & `src/sharepoint_fs_nav.cpp`**:
   - Přidat metodu `const SharePointApi::SharePointItem* FindItemObjectByPanelName(const char* name, BOOL isDir) const;`.
4. **`src/sharepoint_fs_transfer.cpp`**:
   - V `CopyOrMoveFromFS` a `ViewFile`:
     - Vyřešit `driveId` pomocí `ResolveCurrentTarget`.
     - Použít `FindItemObjectByPanelName` pro předání kompletních metadat.
     - Při chybě stahování nejdříve uzavřít `progressDlg->Close()`, zobrazit `SalMessageBox`, nastavit `cancelOrHandlePath = TRUE;` a vrátit `TRUE;`.
5. **Kompilace a testování**:
   - Sestavit plugin pomocí `mingw32-make -f Makefile.mingw`.
   - Zkontrolovat úspěšnost kompilace a vytvoření `sharepoint.spl`, `english.slg`, `czech.slg`.
6. **Aktualizace dokumentace a záznam do gitu**:
   - Aktualizovat `jobs_done.md`, `PLUGIN_DEV.md`, `README.md`, `README_CZ.md`, `implementation_plan.md`.
   - Provést git commit.

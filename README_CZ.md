# Plugin Microsoft SharePoint pro Open Salamander

Plnohodnotný souborový plugin pro správce souborů **Open Salamander** (a Dark Mode fork **KRtkovo-eu-AI/salamander**), který umožňuje procházet, spravovat, nahrávat, stahovat a organizovat soubory a složky na **Microsoft SharePoint** a **OneDrive for Business** přímo ze dvou panelů Salamandera s bleskovou inteligentní mezipamětí.

---

## ✨ Klíčové funkce

### 🚀 Inteligentní paměťová a trvalá disková mezipaměť (Cache)
- **Okamžité procházení (0 ms latence)**: Obsah navštívených složek je uložen v paměti RAM a ukládán na disk do `%APPDATA%\Open Salamander\plugins\sharepoint\cache_<account_hash>.bin`.
- **Synchronizace přes Microsoft Graph Delta API**:
  - Periodická lehká kontrola změn pomocí synchronizačních delta tokenů.
  - Selektivní zneplatnění pouze těch složek, kde došlo ke změně, namísto stahování celých stromů.
  - Nulové zbytečné síťové dotazy, pokud se na vzdáleném disku nic nezměnilo.

### 👥 Správa více účtů (Multi-Account) a flexibilní Tenant ID
- Možnost mít přihlášeno **více Microsoft 365 účtů současně** (např. osobní OneDrive a firemní SharePoint tenant).
- **Podpora libovolného Tenant ID**: Připojení k multitenantním (`common`), firemním (`organizations`) i privátním Azure AD / Entra ID tenantům.
- **100% oddělení dat**: Každý účet má vlastní nezávislou diskovou i paměťovou cache.
- **Šifrované uložení tokenů (Windows DPAPI)**: Bezpečné šifrování v rámci uživatelského profilu Windows.
- **Tichá obnova (Silent Refresh)**: Přístupové tokeny se na pozadí automaticky obnovují bez otevírání prohlížeče.

### 🏢 Kompletní hierarchie SharePointu & OneDrivu
- **`OneDrive`**: Okamžitý přístup k osobním i firemním souborům cloudu (`/v1.0/me/drive/root`).
- **`Sites`**: Plynulé procházení napříč weby SharePointu (`/v1.0/sites`) a jejich Knihovnami dokumentů (`/v1.0/sites/{id}/drives`).
- **`Shared with me`**: Soubory sdílené přímo s přihlášeným uživatelem.
- **`Recent`**: Nedávno otevřené a upravované dokumenty.
- **`Recycle Bin`**: Cloudový koš s možností obnovení a vysypání.

### ✏️ Zápisové a souborové operace
- 📁 **F7 – Vytvoření složky (`CreateDir`)**: Vytváření nových složek v OneDrivu, Knihovnách dokumentů SharePointu i podsložkách s okamžitou aktualizací panelu.
- ✏️ **Shift+F6 – Rychlé přejmenování (`QuickRename`)**: Okamžité přejmenování souborů a složek přímo v panelu.
- 🗑️ **F8 – Mazání a přesun do Koše (`Delete`)**: Přesun položky do Koše SharePointu / OneDrivu.
- 📤 **F5 – Nahrávání na SharePoint / OneDrive**:
  - Jednoduchý přímý upload (<4MB) i výkonný chunked Upload Session pro velké soubory (>4MB).
  - **Předběžný průchod (Pass 1) & dva progress bary**: Předem spočítá přesný počet i objem dat v bajtech a zobrazuje jak průběh aktuálního souboru, tak celkový přenos včetně rychlosti.
  - **Detekce kolizí existujících složek i souborů**: Interaktivní dialog s možnostmi *Sloučit / Přepsat*, *Ponechat oba*, *Přeskočit* a *Použít pro všechny zbývající*.
- 📥 **F5 – Stahování na lokální disk**: Rychlé streamované stahování se stavovým dialogem průběhu.
- 👁️ **F3 – Interní prohlížeč**: Okamžitý náhled vzdálených souborů s automatickou mezipamětí.
- 📊 **Ukazatel volného místa na disku**: Zobrazení volné a celkové kapacity v patičce panelu Salamandera.
- 📋 **Vlastní sloupce v podrobném pohledu (`CSalamanderViewAbstract`)**:
  - **`Autor`**: Zobrazuje jméno nebo e-mail tvůrce položky.
  - **`Upravil`**: Jméno uživatele, který soubor naposledy upravil.
  - **`Sdíleno`**: Indikace, zda je položka sdílena s dalšími uživateli.
  - **`Web URL`**: Přímý odkaz do SharePoint Online / Office Web App.
- 🔎 **Cloudové vyhledávání (`Alt+F7`)**:
  - Rychlé hledání postavené na Microsoft Graph Search API.
  - Volba rozsahu hledání: aktuální web, knihovna, celý OneDrive / SharePoint.
  - Dvojklikem nebo stiskem `Enter` přechod přímo na vybranou položku v panelu.

### ⭐ Virtuální složky a kontextové menu
- *Otevřít ve webovém prohlížeči* (SharePoint Online / Office Web App)
- *Kopírovat odkaz do schránky*
- *Spočítat velikost složky...* (`Alt+F10` / `Mezera` v panelu)

### 🌙 Plná podpora Dark Mode
- Tmavý režim plně ladící s barevným schématem tmavého motivu Open Salamandera.

---

## 🚀 Instalace

1. Stáhněte nejnovější archiv `salamander-sharepoint-plugin.zip`.
2. Rozbalte soubory do adresáře `plugins\sharepoint` v instalaci Open Salamandera:
   ```
   Open Salamander/
   └── plugins/
       └── sharepoint/
           ├── sharepoint.spl
           └── lang/
               ├── english.slg
               └── czech.slg
   ```
3. Spusťte **Open Salamander**, přejděte do **Plugins → Plugins Manager...**, klikněte na **Add...** a vyberte `sharepoint.spl`.
4. V panelu stiskněte **Alt+F1** nebo **Alt+F2** a zvolte disk **sharepoint:**.
5. V menu **Plugins → Microsoft SharePoint → Nastavení...** přidejte svůj Microsoft účet.

---

## ⚙️ Registrace aplikace v Microsoft Azure AD / Entra ID

Pro připojení k vašemu SharePointu a OneDrivu:
1. Přejděte do [Microsoft Entra admin center / Azure Portal](https://portal.azure.com/).
2. Otevřete **Registrace aplikací (App registrations) → Nová registrace (New registration)**.
3. Jako podporované typy účtů zvolte **Účty v libovolném organizačním adresáři a osobní účty Microsoft** (případně pouze pro vaši organizaci).
4. V sekci **Ověřování (Authentication)** přidejte platformu **Mobilní a desktopové aplikace (Mobile and desktop applications)** s URI pro přesměrování: `http://localhost`.
5. V sekci **Oprávnění rozhraní API (API permissions)** přidejte delegovaná oprávnění pro **Microsoft Graph**:
   - `offline_access`
   - `Files.ReadWrite.All`
   - `Sites.ReadWrite.All`
   - `User.Read`
6. Zkopírujte **ID aplikace (klienta)** a případně **ID adresáře (tenanta)**.
7. V Open Salamanderu otevřete **Plugins → Microsoft SharePoint → Nastavení...**, vložte údaje a klikněte na **Přidat účet...**.

---

## 🛠️ Kompilace ze zdrojových kódů (MinGW-w64)

### Požadavky:
- MinGW-w64 (GCC s podporou C++17)
- `mingw32-make`
- Open Salamander SDK (v adresáři `../salamander-plugins/salamand`)

### Příkaz pro kompilaci:
```powershell
mingw32-make -f Makefile.mingw clean
mingw32-make -f Makefile.mingw
```

Příkaz vytvoří:
- `sharepoint.spl` (Hlavní knihovna pluginu)
- `english.slg` (Anglické jazykové zdroje)
- `czech.slg` (České jazykové zdroje)

---

## 📄 Licence a autoři

Licencováno pod **GNU General Public License v2.0 nebo novější** ([GPL-2.0-or-later](LICENSE)).

- Autor: **fila73**
- Inspirováno projektem **Red Salamander**
- Integrace Dark Mode podle forku **Ondrej Kotas (KRtkovo-eu-AI)**.

#include "../src/sharepoint_transfer_logic.h"
#include <cstdio>
#include <cstdlib>
using namespace SharePointTransfer;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d\n", __LINE__); g_fail++; } } while (0)

static void WriteFile_(const std::wstring& p, const char* s)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w; WriteFile(h, s, (DWORD)strlen(s), &w, NULL); CloseHandle(h);
}
static std::string ReadFile_(const std::wstring& p)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return "<missing>";
    char b[256]; DWORD r = 0; ReadFile(h, b, sizeof(b) - 1, &r, NULL); CloseHandle(h);
    return std::string(b, r);
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int main()
{
    std::wstring o;
    CHECK(!MakeSafeLocalComponent(L"", o));
    CHECK(!MakeSafeLocalComponent(L".", o));
    CHECK(!MakeSafeLocalComponent(L"..", o));
    CHECK(!MakeSafeLocalComponent(L"...", o));
    CHECK(MakeSafeLocalComponent(L"a\\b/c:d", o) && o == L"a_b_c_d");
    CHECK(MakeSafeLocalComponent(L"..\\..\\evil", o) && o == L".._.._evil");
    CHECK(o.find_first_of(L"\\/:") == std::wstring::npos);
    CHECK(MakeSafeLocalComponent(L"CON", o) && o == L"_CON");
    CHECK(MakeSafeLocalComponent(L"nul.txt", o) && o == L"_nul.txt");
    CHECK(MakeSafeLocalComponent(L"com1", o) && o == L"_com1");
    CHECK(MakeSafeLocalComponent(L"COM0", o) && o == L"COM0");
    CHECK(MakeSafeLocalComponent(L"console", o) && o == L"console");
    CHECK(MakeSafeLocalComponent(L"report.docx ", o) && o == L"report.docx");

    // reserved device names (extra)
    CHECK(IsReservedDeviceName(L"CONIN$")); CHECK(IsReservedDeviceName(L"conout$.txt"));
    CHECK(IsReservedDeviceName(L"CLOCK$"));
    CHECK(IsReservedDeviceName(L"COM\x00B9")); CHECK(IsReservedDeviceName(L"lpt\x00B2.txt"));
    CHECK(IsReservedDeviceName(L"COM\x00B3")); CHECK(!IsReservedDeviceName(L"COM\x00B4"));
    CHECK(MakeSafeLocalComponent(L"CONOUT$", o) && o == L"_CONOUT$");

    // temp name is unique and never the old fixed ".sppart" name
    CHECK(PartFilePath(L"C:\\x\\a.txt", 1) != PartFilePath(L"C:\\x\\a.txt", 2));
    CHECK(PartFilePath(L"C:\\x\\a.txt", 1).find(L"C:\\x\\a.txt") == 0);
    CHECK(PartFilePath(L"C:\\x\\a.txt", 1) != L"C:\\x\\a.txt.sppart");

    // local name clashes (case-insensitive)
    { LocalNameSet ns; CHECK(ns.Add(L"a.txt")); CHECK(!ns.Add(L"A.TXT")); CHECK(ns.Add(L"b.txt")); }

    // reparse-point classification
    CHECK(ClassifyLocalEntry(FILE_ATTRIBUTE_DIRECTORY).recurse);
    CHECK(!ClassifyLocalEntry(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT).recurse);
    CHECK(!ClassifyLocalEntry(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT).mayDelete);
    CHECK(ClassifyLocalEntry(FILE_ATTRIBUTE_REPARSE_POINT).upload && !ClassifyLocalEntry(FILE_ATTRIBUTE_REPARSE_POINT).mayDelete);
    CHECK(ClassifyLocalEntry(FILE_ATTRIBUTE_NORMAL).mayDelete);
    CHECK(!GetLocalEntryPolicy(L"C:\\definitely\\missing\\x").mayDelete);

    // conflict state
    ConflictState cs;
    cs.Remember(ConflictChoice::Overwrite, false); CHECK(!cs.hasAll);
    cs.Remember(ConflictChoice::Cancel, true); CHECK(!cs.hasAll);
    cs.Remember(ConflictChoice::Skip, true); CHECK(cs.hasAll && cs.all == ConflictChoice::Skip);

    // delete-source decisions
    ResultNode f1; f1.transferred = true;
    ResultNode f2; f2.transferred = false;
    CHECK(ShouldDeleteSource(f1)); CHECK(!ShouldDeleteSource(f2));
    ResultNode d; d.isDir = true; d.children = {f1, f1};
    CHECK(ShouldDeleteSource(d));
    d.children.push_back(f2); CHECK(!ShouldDeleteSource(d));
    ResultNode empty; empty.isDir = true; CHECK(ShouldDeleteSource(empty));
    empty.dirReady = false; CHECK(!ShouldDeleteSource(empty));
    ResultNode outer; outer.isDir = true; ResultNode inner = d; outer.children = {f1, inner};
    CHECK(!ShouldDeleteSource(outer));
    inner.children.pop_back(); outer.children = {f1, inner}; CHECK(ShouldDeleteSource(outer));
    { ResultNode k; k.transferred = true; k.keep = true; CHECK(!ShouldDeleteSource(k)); }
    { ResultNode k; k.transferred = true; k.keep = true; ResultNode p; p.isDir = true; p.children = {f1, k};
      CHECK(!ShouldDeleteSource(p)); }

    // temp files on real files
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sptest_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring target = dir + L"\\a.txt", part;

    // never touches an existing "a.txt.sppart" user file; names are unique
    WriteFile_(target, "ORIGINAL");
    WriteFile_(target + L".sppart", "USERFILE");
    {
        std::wstring p1, p2;
        HANDLE h1 = CreatePartFile(target, p1), h2 = CreatePartFile(target, p2);
        CHECK(h1 != INVALID_HANDLE_VALUE && h2 != INVALID_HANDLE_VALUE && p1 != p2);
        CloseHandle(h1); CloseHandle(h2);
        CHECK(ReadFile_(target + L".sppart") == "USERFILE");
        DiscardPartFile(p1); DiscardPartFile(p2); DiscardPartFile(L"");
        CHECK(!Exists(p1) && !Exists(p2));
        CHECK(ReadFile_(target + L".sppart") == "USERFILE");
        CHECK(ReadFile_(target) == "ORIGINAL");
        DeleteFileW((target + L".sppart").c_str());
    }
    {
        HANDLE h = CreatePartFile(target, part); CloseHandle(h);
        CHECK(Exists(part));
    }
    WriteFile_(part, "NEWDATA");
    CHECK(CommitPartFile(part, target));
    CHECK(ReadFile_(target) == "NEWDATA"); CHECK(!Exists(part));

    // failure/cancel: discard temp only, original untouched
    {
        HANDLE h = CreatePartFile(target, part); CloseHandle(h);
        WriteFile_(part, "PARTIAL");
        DiscardPartFile(part);
        CHECK(!Exists(part)); CHECK(ReadFile_(target) == "NEWDATA");
    }

    // commit with missing part: fails, original untouched
    CHECK(!CommitPartFile(part, target)); CHECK(ReadFile_(target) == "NEWDATA");

    // commit when target does not exist
    DeleteFileW(target.c_str());
    {
        HANDLE h = CreatePartFile(target, part); CloseHandle(h);
        WriteFile_(part, "FRESH");
        CHECK(CommitPartFile(part, target)); CHECK(ReadFile_(target) == "FRESH");
    }
    DeleteFileW(target.c_str());

    // real junctions: never recursed into, never deleted through (incl. a self-referencing one)
    {
        std::wstring real = dir + L"\\real", jn = dir + L"\\junc", loop = dir + L"\\loop";
        CreateDirectoryW(real.c_str(), NULL);
        WriteFile_(real + L"\\keep.txt", "REAL");
        std::wstring cmd = L"cmd /c \"mklink /J \"" + jn + L"\" \"" + real + L"\" >nul && mklink /J \"" +
                           loop + L"\" \"" + dir + L"\" >nul\"";
        if (_wsystem(cmd.c_str()) == 0)
        {
            LocalEntryPolicy pj = GetLocalEntryPolicy(jn);
            CHECK(!pj.recurse && !pj.mayDelete && pj.reportSkipped);
            CHECK(!GetLocalEntryPolicy(loop).recurse);
            CHECK(GetLocalEntryPolicy(real).recurse);
            CHECK(Exists(jn + L"\\keep.txt")); // the junction really resolves
            RemoveDirectoryW(jn.c_str()); RemoveDirectoryW(loop.c_str()); // removes only the links
            CHECK(ReadFile_(real + L"\\keep.txt") == "REAL");
        }
        else
        {
            printf("FAIL: mklink /J unavailable\n"); g_fail++;
        }
        DeleteFileW((real + L"\\keep.txt").c_str()); RemoveDirectoryW(real.c_str());
    }
    RemoveDirectoryW(dir.c_str());
    printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}

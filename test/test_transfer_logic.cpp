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

    CHECK(PartFilePath(L"C:\\x\\a.txt") == L"C:\\x\\a.txt.sppart");

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

    // temp file replace on real files
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sptest_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring target = dir + L"\\a.txt", part = PartFilePath(target);

    WriteFile_(target, "ORIGINAL");
    WriteFile_(part, "NEWDATA");
    CHECK(CommitPartFile(part, target));
    CHECK(ReadFile_(target) == "NEWDATA"); CHECK(!Exists(part));

    // failure/cancel: discard temp only, original untouched
    WriteFile_(part, "PARTIAL");
    DiscardPartFile(part);
    CHECK(!Exists(part)); CHECK(ReadFile_(target) == "NEWDATA");

    // commit with missing part: fails, original untouched
    CHECK(!CommitPartFile(part, target)); CHECK(ReadFile_(target) == "NEWDATA");

    // commit when target does not exist
    DeleteFileW(target.c_str());
    WriteFile_(part, "FRESH");
    CHECK(CommitPartFile(part, target)); CHECK(ReadFile_(target) == "FRESH");

    DeleteFileW(target.c_str()); RemoveDirectoryW(dir.c_str());
    printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}

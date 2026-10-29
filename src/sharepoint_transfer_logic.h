// SPDX-FileCopyrightText: 2026 fila73
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure (UI/network free) helpers for safe transfers. Header-only so that it
// can be unit-tested standalone (see test\build_test_transfer_logic.bat).

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <set>

namespace SharePointTransfer
{

// ---- safe local names ------------------------------------------------------

inline bool IsReservedDeviceName(const std::wstring& name)
{
    // Device name is reserved also with an extension ("CON.txt").
    std::wstring stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    std::wstring up;
    for (wchar_t c : stem) up += (wchar_t)towupper(c);
    if (up == L"CON" || up == L"PRN" || up == L"AUX" || up == L"NUL" ||
        up == L"CONIN$" || up == L"CONOUT$" || up == L"CLOCK$")
        return true;
    // COM1-9 / LPT1-9 plus the superscript digits U+00B9, U+00B2, U+00B3 (also reserved by Windows)
    if (up.size() == 4 && (up.compare(0, 3, L"COM") == 0 || up.compare(0, 3, L"LPT") == 0) &&
        ((up[3] >= L'1' && up[3] <= L'9') || up[3] == 0x00B9 || up[3] == 0x00B2 || up[3] == 0x00B3))
        return true;
    return false;
}

// Turns a remote name into one safe local path component.
// Returns false (and leaves out empty) for names that must be rejected: empty, "." or "..".
inline bool MakeSafeLocalComponent(const std::wstring& remote, std::wstring& out)
{
    out.clear();
    if (remote.empty() || remote == L"." || remote == L"..") return false;
    std::wstring r = remote;
    for (auto& c : r)
    {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
            c == L'<' || c == L'>' || c == L'|' || c < 32)
            c = L'_';
    }
    while (!r.empty() && (r.back() == L' ' || r.back() == L'.')) r.pop_back();
    if (r.empty()) return false; // e.g. "..." or "   "
    if (IsReservedDeviceName(r)) r.insert(r.begin(), L'_');
    out = r;
    return true;
}

// ---- temporary file next to the target -------------------------------------

// Unique temp name next to the target (never a name a user or a remote item could own).
inline std::wstring PartFilePath(const std::wstring& target, unsigned seq)
{
    wchar_t suffix[64];
    swprintf(suffix, 64, L".~%08lx%04x.sppart", (unsigned long)GetTickCount(), seq & 0xFFFF);
    return target + suffix;
}

// Creates a brand-new temp file (CREATE_NEW: an existing file is never touched, retried with
// another name). Returns INVALID_HANDLE_VALUE on failure; partPath is the file this call created.
inline HANDLE CreatePartFile(const std::wstring& target, std::wstring& partPath)
{
    static volatile LONG counter = 0;
    for (int attempt = 0; attempt < 100; attempt++)
    {
        unsigned seq = (unsigned)InterlockedIncrement(&counter) + GetCurrentProcessId() * 7919u;
        std::wstring p = PartFilePath(target, seq);
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            partPath = p;
            return h;
        }
        DWORD e = GetLastError();
        if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) break;
    }
    partPath.clear();
    return INVALID_HANDLE_VALUE;
}

// Replaces target with the finished temp file. On failure the temp file is
// removed and the original target is left untouched.
inline bool CommitPartFile(const std::wstring& part, const std::wstring& target)
{
    if (MoveFileExW(part.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(part.c_str());
    return false;
}

// Removes only the temp file this operation created (failure / cancel path).
inline void DiscardPartFile(const std::wstring& part)
{
    if (!part.empty()) DeleteFileW(part.c_str());
}

// ---- local names written in one operation ------------------------------------

// Case-insensitive set of local names; Add returns false for a clash (skip the later item).
class LocalNameSet
{
public:
    bool Add(const std::wstring& name)
    {
        std::wstring k;
        for (wchar_t c : name) k += (wchar_t)towlower(c);
        return m_set.insert(k).second;
    }

private:
    std::set<std::wstring> m_set;
};

// ---- local source classification for Upload / Move -------------------------------

constexpr uint32_t kAttrDirectory = 0x00000010;    // FILE_ATTRIBUTE_DIRECTORY
constexpr uint32_t kAttrReparsePoint = 0x00000400; // FILE_ATTRIBUTE_REPARSE_POINT
constexpr int kMaxLocalDepth = 64;

struct LocalEntryPolicy
{
    bool recurse;       // descend into the directory
    bool upload;        // upload (the content of) the item
    bool mayDelete;     // Move may delete the local item after it was transferred
    bool reportSkipped; // tell the user the item was skipped / not deleted
};

inline LocalEntryPolicy ClassifyLocalEntry(uint32_t attrs)
{
    bool dir = (attrs & kAttrDirectory) != 0;
    bool reparse = (attrs & kAttrReparsePoint) != 0;
    if (dir && reparse) return {false, false, false, true}; // junction / symlinked dir: never followed
    if (dir) return {true, false, true, false};
    if (reparse) return {false, true, false, true};         // symlinked file: content uploaded, link kept
    return {false, true, true, false};
}

// Policy of a local path, looked at WITHOUT following reparse points. Unreadable = untouchable.
inline LocalEntryPolicy GetLocalEntryPolicy(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return {false, false, false, false};
    return ClassifyLocalEntry((uint32_t)a);
}

// ---- conflict policy ---------------------------------------------------------

enum class ConflictChoice
{
    Overwrite,
    Skip,
    Cancel
};

// Remembers "apply to all" decisions (Overwrite all / Skip all) for one batch.
struct ConflictState
{
    bool hasAll = false;
    ConflictChoice all = ConflictChoice::Skip;

    void Remember(ConflictChoice c, bool applyToAll)
    {
        if (applyToAll && c != ConflictChoice::Cancel)
        {
            hasAll = true;
            all = c;
        }
    }
};

// ---- result tree for Move decisions ----------------------------------------

struct ResultNode
{
    bool isDir = false;
    std::wstring path;         // local path (upload source)
    std::string remoteId;      // remote item id (download source)
    bool transferred = false; // file: really transferred (not skipped / failed)
    bool dirReady = true;     // dir: created / merged successfully
    bool keep = false;        // source must stay (reparse point, name clash): never deleted
    std::vector<ResultNode> children;
};

// A source file may be deleted only when it was transferred; a source folder
// only when it was created and every child may be deleted.
inline bool ShouldDeleteSource(const ResultNode& n)
{
    if (n.keep) return false;
    if (!n.isDir) return n.transferred;
    if (!n.dirReady) return false;
    for (const auto& c : n.children)
        if (!ShouldDeleteSource(c)) return false;
    return true;
}

} // namespace SharePointTransfer

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
    if (up == L"CON" || up == L"PRN" || up == L"AUX" || up == L"NUL") return true;
    if (up.size() == 4 && (up.compare(0, 3, L"COM") == 0 || up.compare(0, 3, L"LPT") == 0) &&
        up[3] >= L'1' && up[3] <= L'9')
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

inline std::wstring PartFilePath(const std::wstring& target)
{
    return target + L".sppart";
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

// Removes only the temp file (failure / cancel path).
inline void DiscardPartFile(const std::wstring& part)
{
    DeleteFileW(part.c_str());
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
    bool transferred = false; // file: really transferred (not skipped / failed)
    bool dirReady = true;     // dir: created / merged successfully
    std::vector<ResultNode> children;
};

// A source file may be deleted only when it was transferred; a source folder
// only when it was created and every child may be deleted.
inline bool ShouldDeleteSource(const ResultNode& n)
{
    if (!n.isDir) return n.transferred;
    if (!n.dirReady) return false;
    for (const auto& c : n.children)
        if (!ShouldDeleteSource(c)) return false;
    return true;
}

} // namespace SharePointTransfer

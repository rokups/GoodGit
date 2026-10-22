#include "StatusPorcelain.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace ggui::core {

std::string conflictDescription(bool s1, bool s2, bool s3)
{
    static const char* const kByStages[8] = {"both deleted", "added by them", "added by us", "both added",
        "both deleted", "deleted by us", "deleted by them", "both modified"};
    return kByStages[(int(s1) << 2) | (int(s2) << 1) | int(s3)];
}

namespace {

[[noreturn]] void malformed(char type, std::string_view record)
{
    throw std::runtime_error(std::string("malformed porcelain v2 '") + type + "' record: " + std::string(record));
}

// Splits `count` leading fields on single spaces. The rest of the record is the path (it may hold
// spaces). Throws when a field or the path is missing.
std::string_view splitFields(char type, std::string_view record, size_t count, std::string_view* fields)
{
    std::string_view rest = record;
    for (size_t i = 0; i < count; ++i) {
        const size_t sp = rest.find(' ');
        if (sp == std::string_view::npos)
            malformed(type, record);
        fields[i] = rest.substr(0, sp);
        rest.remove_prefix(sp + 1);
    }
    if (rest.empty())
        malformed(type, record);
    return rest;
}

ChangeKind kindFromLetter(char c)
{
    switch (c) {
    case 'A': return ChangeKind::Added;
    case 'D': return ChangeKind::Deleted;
    case 'T': return ChangeKind::TypeChanged;
    case 'R': return ChangeKind::Renamed;
    case 'C': return ChangeKind::Copied;
    default: return ChangeKind::Modified;
    }
}

// The XY field must hold two characters and the submodule field four.
void checkShape(char type, std::string_view record, std::string_view xy, std::string_view sub)
{
    if (xy.size() != 2 || sub.size() != 4)
        malformed(type, record);
}

// Adds the staged (X) and unstaged (Y) entries of a type 1 or type 2 record.
void addChanges(StatusResult& status, std::string_view xy, std::string_view sub, std::string_view path,
    std::string_view oldPath)
{
    const bool isSub = sub[0] == 'S';
    auto make = [&](char letter) {
        StatusEntry e;
        e.path = std::string(path);
        e.kind = kindFromLetter(letter);
        e.submodule = isSub;
        if (letter == 'R' || letter == 'C')
            e.oldPath = std::string(oldPath);
        return e;
    };
    if (xy[0] != '.')
        status.staged.push_back(make(xy[0]));
    if (xy[1] == 'R' || xy[1] == 'C') {
        // `--renames` pairs a deleted path with an intent-to-add path (`git add -N`) on the
        // unstaged side. Show the two changes: the old path as deleted (not for a copy, the old
        // path stays) and the new path as added.
        if (xy[1] == 'R') {
            StatusEntry gone = make('D');
            gone.path = std::string(oldPath);
            status.unstaged.push_back(std::move(gone));
        }
        StatusEntry added = make('A');
        added.intentToAdd = xy[0] == '.';
        status.unstaged.push_back(std::move(added));
    } else if (xy[1] != '.') {
        StatusEntry e = make(xy[1]);
        if (xy[0] == '.' && xy[1] == 'A')
            e.intentToAdd = true; // `git add -N`: shown as an unstaged addition
        if (isSub) {
            if (sub[1] == 'C')
                e.submoduleState |= NewCommits;
            if (sub[2] == 'M')
                e.submoduleState |= ModifiedContent;
            if (sub[3] == 'U')
                e.submoduleState |= UntrackedContent;
            if (xy[1] == 'D')
                e.submoduleState |= Missing;
        }
        status.unstaged.push_back(std::move(e));
    }
}

} // namespace

ParsedStatus parsePorcelainV2(std::string_view out)
{
    ParsedStatus result;
    StatusResult& status = result.status;

    while (!out.empty()) {
        const size_t nul = out.find('\0');
        const std::string_view record = out.substr(0, nul);
        out.remove_prefix(nul == std::string_view::npos ? out.size() : nul + 1);
        if (record.empty())
            continue;

        std::string_view f[10];
        switch (record[0]) {
        case '?': {
            if (record.size() < 3 || record[1] != ' ')
                malformed('?', record);
            StatusEntry e;
            e.path = std::string(record.substr(2));
            e.kind = ChangeKind::Untracked;
            status.untracked.push_back(std::move(e));
            break;
        }
        case '1': {
            // 1 XY sub mH mI mW hH hI path
            const std::string_view path = splitFields('1', record, 8, f);
            checkShape('1', record, f[1], f[2]);
            addChanges(status, f[1], f[2], path, {});
            break;
        }
        case '2': {
            // 2 XY sub mH mI mW hH hI Xscore path NUL origPath
            const std::string_view path = splitFields('2', record, 9, f);
            checkShape('2', record, f[1], f[2]);
            if (out.empty())
                malformed('2', record); // the original path field is missing
            const size_t next = out.find('\0');
            const std::string_view oldPath = out.substr(0, next);
            out.remove_prefix(next == std::string_view::npos ? out.size() : next + 1);
            addChanges(status, f[1], f[2], path, oldPath);
            break;
        }
        case 'u': {
            // u XY sub m1 m2 m3 mW h1 h2 h3 path
            const std::string_view path = splitFields('u', record, 10, f);
            checkShape('u', record, f[1], f[2]);
            StatusEntry e;
            e.path = std::string(path);
            e.kind = ChangeKind::Conflicted;
            e.stage1 = f[3] != "000000";
            e.stage2 = f[4] != "000000";
            e.stage3 = f[5] != "000000";
            e.conflictDescription = conflictDescription(e.stage1, e.stage2, e.stage3);
            e.submodule = f[2][0] == 'S';
            status.conflicted.push_back(std::move(e));
            result.conflictOids.push_back({std::string(f[7]), std::string(f[8]), std::string(f[9])});
            break;
        }
        default: break; // '#' headers, '!' ignored entries and unknown records
        }
    }

    auto byPath = [](const StatusEntry& a, const StatusEntry& b) { return a.path < b.path; };
    std::stable_sort(status.staged.begin(), status.staged.end(), byPath);
    std::stable_sort(status.unstaged.begin(), status.unstaged.end(), byPath);
    std::stable_sort(status.untracked.begin(), status.untracked.end(), byPath);

    // Sort the conflicts and their object ids together.
    std::vector<size_t> order(status.conflicted.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(),
        [&](size_t a, size_t b) { return status.conflicted[a].path < status.conflicted[b].path; });
    std::vector<StatusEntry> conflicted;
    std::vector<ParsedStatus::ConflictOids> oids;
    conflicted.reserve(order.size());
    oids.reserve(order.size());
    for (const size_t i : order) {
        conflicted.push_back(std::move(status.conflicted[i]));
        oids.push_back(std::move(result.conflictOids[i]));
    }
    status.conflicted = std::move(conflicted);
    result.conflictOids = std::move(oids);
    return result;
}

} // namespace ggui::core

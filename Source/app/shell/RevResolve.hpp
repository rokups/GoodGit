// Resolving what a dialog's commit field says to a commit, synchronously and without git: from the
// loaded History rows and the snapshot's refs (the commit preview under a Field::Commit input).
#pragma once

#include "shell/Dialogs.hpp"

#include <core/Types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ggui {

class Session;

using RowLookup = std::function<const core::HistoryRow*(const core::Oid&)>;

// `text` as git reads it, for what is loaded: HEAD (or @), a local or remote branch, a tag (also
// with their refs/... prefixes), a full id or a unique id prefix of 4+ characters, and any of
// those followed by ~N, ^ and ^N (parents among the loaded rows). Null when unknown, ambiguous
// or not loaded.
const core::HistoryRow* resolveRev(const core::Snapshot& snap, const std::vector<core::HistoryRow>& rows,
    const RowLookup& lookup, const std::string& text);
const core::HistoryRow* resolveRev(Session& session, const std::string& text);

// A commit as one line: "a1b2c3d Subject", the subject cut to `maxChars` characters (0: whole).
std::string commitLine(const core::HistoryRow& row, size_t maxChars = 0);
std::string commitLine(Session& session, const core::Oid& id, size_t maxChars = 0);

// A commit input prefilled with `prefill`, previewing the commit it resolves to. `emptyLabel` and
// `emptyTarget`: what an empty field means (e.g. "the parent"), shown as its preview.
Field commitField(Session& session, const std::string& id, const std::string& label, const std::string& prefill,
    const std::string& emptyLabel = {}, const core::Oid& emptyTarget = {});
// An Info row "<label>: a1b2c3d Subject" naming a fixed commit of the dialog.
Field commitInfo(Session& session, const std::string& label, const core::Oid& id);
// The same for a revision the dialog was opened with (branch name, "HEAD", id...).
Field commitInfo(Session& session, const std::string& label, const std::string& rev);

} // namespace ggui

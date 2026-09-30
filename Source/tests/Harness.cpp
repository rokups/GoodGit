#include "tests/Harness.hpp"

#include "shell/App.hpp"
#include "shell/Dialogs.hpp"
#include "panels/HistoryPanel.hpp"
#include "shell/Session.hpp"
#include "shell/Theme.hpp"
#include "util/Env.hpp"

#include <imgui_internal.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <functional>
#include <map>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#undef Yield // winbase.h's, not ImGuiTestContext::Yield
#endif

namespace ggtest {

namespace {

// Fixture commits get deterministic, increasing dates so history order is stable.
constexpr long long kEpoch = 1767225600; // 2026-01-01T00:00:00Z

} // namespace

Scenario::Scenario(ImGuiTestContext* c, ggui::App& a, fs::path root, std::uint64_t seed)
    : ctx(c), app(a), m_root(std::move(root)), m_seed(seed), m_rng(seed)
{
}

bool Scenario::gitAtLeast(int major, int minor, const char* skipWhy)
{
    const int version = gg::gitVersion(m_root);
    if (version >= major * 100 + minor)
        return true;
    if (skipWhy) {
        const std::string reason = "needs git " + std::to_string(major) + "." + std::to_string(minor) + " or newer ("
            + skipWhy + "); git on PATH is " + std::to_string(version / 100) + "." + std::to_string(version % 100);
        ctx->LogWarning("skipped: %s", reason.c_str());
        markCurrentTestSkipped(reason);
    }
    return false;
}

gg::RunResult Scenario::gitMayFail(const fs::path& cwd, std::vector<std::string> args, std::string input)
{
    gg::RunRequest r;
    r.args.reserve(args.size() + 1);
    r.args.emplace_back("git");
    for (auto& a : args)
        r.args.push_back(std::move(a));
    r.cwd = cwd;
    r.input = std::move(input);
    const long long when = kEpoch + 60LL * (++m_commitCounter);
    const std::string date = "@" + std::to_string(when) + " +0000";
    r.env.emplace_back("GIT_AUTHOR_DATE", date);
    r.env.emplace_back("GIT_COMMITTER_DATE", date);
    // Test reads (git status / diff) must not take index.lock while ggui mutates.
    r.env.emplace_back("GIT_OPTIONAL_LOCKS", "0");
    return gg::run(r);
}

gg::RunResult Scenario::git(const fs::path& cwd, std::vector<std::string> args, std::string input)
{
    std::string cmd = "git";
    for (const auto& a : args)
        cmd += " " + a;
    gg::RunResult result = gitMayFail(cwd, std::move(args), std::move(input));
    if (!result.ok()) {
        ctx->LogError("step failed (%d): %s\n  in %s\n%s", result.exitCode, cmd.c_str(), cwd.string().c_str(),
            result.message().c_str());
        const bool ok = false;
        IM_CHECK_NO_RET(ok);
    }
    return result;
}

std::string Scenario::gitOut(const fs::path& cwd, std::vector<std::string> args)
{
    return gg::trim(git(cwd, std::move(args)).out);
}

gg::RunResult Scenario::gitgg(const fs::path& cwd, std::vector<std::string> args, std::string input)
{
    args.insert(args.begin(), "gg");
    return gitMayFail(cwd, std::move(args), std::move(input));
}

gg::RunResult Scenario::run(const fs::path& cwd, std::vector<std::string> args, std::string input)
{
    gg::RunRequest r;
    r.args = std::move(args);
    r.cwd = cwd;
    r.input = std::move(input);
    return gg::run(r);
}

void Scenario::write(const fs::path& repo, const std::string& rel, const std::string& content)
{
    const fs::path p = repo / rel;
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

std::string Scenario::read(const fs::path& repo, const std::string& rel)
{
    std::ifstream f(repo / rel, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void Scenario::commitFile(const fs::path& repo, const std::string& rel, const std::string& content, const std::string& message)
{
    write(repo, rel, content);
    git(repo, {"add", "--", rel});
    git(repo, {"commit", "-q", "-m", message});
}

void Scenario::track(const fs::path& repo)
{
    for (const auto& t : m_tracked)
        if (t == repo)
            return;
    m_tracked.push_back(repo);
}

std::string Scenario::head(const fs::path& repo)
{
    auto r = gitMayFail(repo, {"rev-parse", "--verify", "-q", "HEAD"});
    return r.ok() ? gg::trim(r.out) : std::string();
}

std::string Scenario::revParse(const fs::path& repo, const std::string& rev)
{
    return gitOut(repo, {"rev-parse", "--verify", rev});
}

std::vector<std::string> Scenario::refs(const fs::path& repo)
{
    return gg::splitLines(git(repo, {"for-each-ref", "--format=%(refname) %(objectname)"}).out);
}

std::string Scenario::statusPorcelain(const fs::path& repo)
{
    return git(repo, {"status", "--porcelain=v2", "-z", "-uall"}).out;
}

std::map<std::string, std::string> Scenario::gitDirBytes(const fs::path& repo)
{
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(repo / ".git")) {
        if (!e.is_regular_file())
            continue;
        if (fs::relative(e.path(), repo / ".git").generic_string().rfind("gg/cache/", 0) == 0)
            continue; // disposable caches (the history's conflict scan writes one on open)
        std::ifstream f(e.path(), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        out[fs::relative(e.path(), repo).generic_string()] = ss.str();
    }
    return out;
}

bool Scenario::gitTransparent(const fs::path& repo, std::string* why)
{
    std::string problems;
    const auto common = gitMayFail(repo, {"rev-parse", "--path-format=absolute", "--git-common-dir"});
    if (!common.ok())
        return true; // not a repository any more (fsck reports it)
    for (const auto& ref : gg::splitLines(gitMayFail(repo, {"for-each-ref", "--format=%(refname)", "refs/gg"}).out))
        if (!ref.empty())
            problems += "\n  ref written under refs/gg/: " + ref;
    const fs::path ggDir = fs::path(gg::trim(common.out)) / "gg";
    std::error_code ec;
    if (fs::is_directory(ggDir, ec)) {
        for (const auto& e : fs::directory_iterator(ggDir, ec)) {
            const std::string name = e.path().filename().string();
            const bool dir = e.is_directory();
            // journal (+ .lock): undo history. cache/: disposable (conflict scan). hooks/: the managed-hook
            // runner (H1). rebase/: journal grouping of a native rebase in progress. edit/: Edit commit
            // sessions (§4.3). symref-*: a hook's note between the prepared and committed
            // reference-transaction calls.
            const bool ok = (!dir && (name == "journal" || name == "journal.lock" || name.rfind("symref-", 0) == 0))
                || (dir && (name == "cache" || name == "hooks" || name == "rebase" || name == "edit"));
            if (!ok)
                problems += "\n  unexpected entry in " + ggDir.generic_string() + ": " + name;
        }
    }
    if (why)
        *why = problems;
    return problems.empty();
}

bool Scenario::fsck(const fs::path& repo, std::string* output)
{
    auto r = gitMayFail(repo, {"fsck", "--no-progress", "--strict"});
    // A repository plain git understands: status (non-bare) and for-each-ref must work too.
    auto refsOk = gitMayFail(repo, {"for-each-ref", "--count=1"});
    auto bare = gitMayFail(repo, {"rev-parse", "--is-bare-repository"});
    bool statusOk = true;
    if (bare.ok() && gg::trim(bare.out) == "false")
        statusOk = gitMayFail(repo, {"status", "--porcelain=v2"}).ok();
    if (output)
        *output = r.out + r.err;
    return r.ok() && refsOk.ok() && statusOk;
}

bool Scenario::waitUntil(const std::function<bool()>& pred, float seconds)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0f));
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline || ctx->IsError())
            return pred();
        ctx->Yield();
    }
    return true;
}

bool Scenario::waitIdle(float seconds)
{
    ctx->Yield(2);
    const bool ok = waitUntil([&] { return app.idle(); }, seconds);
    ctx->Yield(2);
    return ok && waitUntil([&] { return app.idle(); }, seconds);
}

bool Scenario::openRepository(const fs::path& repo)
{
    track(repo);
    if (ctx->IsError())
        return false;
    if (app.session())
        app.closeRepository();
    const bool welcome = waitUntil([&] {
        ImGuiWindow* w = ctx->GetWindowByRef("//Welcome");
        return w && w->Active && !app.session();
    });
    if (!welcome) {
        ctx->LogError("Welcome screen did not appear");
        IM_CHECK_NO_RET(welcome);
        return false;
    }
    // ItemInputValue presses Enter, which opens the typed path (the keyboard path).
    ctx->ItemInputValue("//Welcome/##welcome_path", repo.string().c_str());
    const bool opened = waitUntil([&] { return app.session() && app.session()->opened(); }, 30.0f);
    if (!opened)
        ctx->LogError("repository did not open: %s (%s)", repo.string().c_str(), app.errorMessage().c_str());
    IM_CHECK_NO_RET(opened);
    waitIdle();
    ctx->SetRef("");
    return opened && !ctx->IsError() && app.session() != nullptr;
}

Scenario::~Scenario()
{
    // By the pids read when the daemons started: the test's directory (with the pid files) may
    // already be gone.
    for (const auto& pid : m_daemonPids)
        run(fs::temp_directory_path(), {"kill", pid});
#ifdef _WIN32
    for (void* job : m_daemonJobs)
        CloseHandle(static_cast<HANDLE>(job)); // kill-on-close: ends the daemon and its children
#endif
    ggui::unsetEnv("GIT_SSH_COMMAND");
}

std::string Scenario::startGitDaemon(const fs::path& baseDir)
{
    // Pick a free port by trying a few; git daemon exits when the port is taken. It is ready when
    // it answers: a repository it does not serve is a "remote error" (any platform's wording).
    auto ready = [&](const std::string& url, const std::function<bool()>& alive) {
        for (int i = 0; i < 100 && alive(); ++i) {
            if (gitMayFail(m_root, {"ls-remote", url + "does-not-exist"}).err.find("remote error") != std::string::npos)
                return true;
            ctx->SleepNoSkip(0.05f, 0.05f);
        }
        return false;
    };
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int port = 20000 + static_cast<int>(m_rng() % 30000);
        const std::string url = "git://127.0.0.1:" + std::to_string(port) + "/";
        std::vector<std::string> args{"daemon", "--reuseaddr", "--listen=127.0.0.1", "--port=" + std::to_string(port),
            "--base-path=" + baseDir.string(), "--export-all", "--enable=receive-pack"};
#ifdef _WIN32
        // No --detach on Windows: started here, in a job object that ends it (and its children).
        const fs::path git = gg::findInPath("git");
        std::wstring cmd = L"\"" + git.wstring() + L"\"";
        for (const auto& a : args)
            cmd += L" \"" + fs::path(a).wstring() + L"\"";
        cmd += L" \"" + baseDir.wstring() + L"\"";
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const std::wstring cwd = m_root.wstring();
        if (!job || !CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
                        nullptr, cwd.c_str(), &si, &pi)) {
            if (job)
                CloseHandle(job);
            continue;
        }
        AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        const HANDLE process = pi.hProcess;
        const bool up = ready(url, [process] { return WaitForSingleObject(process, 0) == WAIT_TIMEOUT; });
        CloseHandle(process);
        if (!up) {
            CloseHandle(job); // ends it
            continue;
        }
        m_daemonJobs.push_back(job);
        return url;
#else
        const fs::path pidFile = m_root / ("daemon-" + std::to_string(m_daemonPids.size()) + ".pid");
        args.push_back("--detach");
        args.push_back("--pid-file=" + pidFile.string());
        args.push_back(baseDir.string());
        if (!gitMayFail(m_root, args).ok())
            continue;
        // The pid is read now: the test's directory (with the file) may be gone when it ends.
        const bool up = ready(url, [] { return true; });
        if (const std::string pid = gg::trim(read(pidFile.parent_path(), pidFile.filename().string())); !pid.empty())
            m_daemonPids.push_back(pid);
        if (up)
            return url;
#endif
    }
    IM_CHECK_NO_RET(false && "git daemon did not start");
    return {};
}

std::string Scenario::installSshShim(const std::string& password)
{
    const fs::path shim = m_root / "fake-ssh";
    std::string script = "#!/bin/sh\n"
                         "# Test SSH shim: runs the remote command locally.\n"
                         "# git probes the ssh variant with -G (OpenSSH prints its config): nothing to connect.\n"
                         "for a in \"$@\"; do [ \"$a\" = -G ] && exit 0; done\n"
                         "while [ $# -gt 0 ]; do case \"$1\" in -o|-p|-i|-l) shift 2;; -*) shift;; *) break;; esac; done\n"
                         "shift\n";
    if (!password.empty())
        script += "answer=$(\"$SSH_ASKPASS\" \"test@localhost's password: \") || exit 255\n"
                  "[ \"$answer\" = \"" + password + "\" ] || { echo 'Permission denied' >&2; exit 255; }\n";
    // git sends ssh://host/D:/x as '/D:/x' (Windows): the local path is D:/x.
    script += "cmd=$(printf '%s' \"$*\" | sed \"s#'/\\([A-Za-z]\\):/#'\\1:/#g\")\n"
              "exec sh -c \"$cmd\"\n";
    write(m_root, "fake-ssh", script);
    fs::permissions(shim, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
    ggui::setEnv("GIT_SSH_COMMAND", shim.generic_string());
    return "ssh://test@localhost";
}

std::string Scenario::sshUrl(const fs::path& repo)
{
    const std::string path = repo.generic_string();
    return "ssh://test@localhost" + std::string(path.empty() || path.front() != '/' ? "/" : "") + path;
}

bool Scenario::itemExists(const char* ref) { return ctx->ItemExists(ref); }

std::string Scenario::itemText(const char* ref)
{
    // Clipped items have no label in the test engine: scroll them into view first.
    if (ctx->ItemExists(ref))
        ctx->ScrollToItemY(ref);
    std::string label = itemLabel(ref);
    const auto pos = label.find("##");
    return pos == std::string::npos ? label : label.substr(0, pos);
}

ggui::Session* Scenario::session() { return app.session(); }

bool Scenario::dialogOpen(const char* title, float seconds)
{
    const bool ok = waitUntil([&] {
        const ggui::Form* f = app.dialogs().current();
        return f && f->title == title && ctx->GetWindowByRef((std::string("//") + title).c_str()) != nullptr;
    }, seconds);
    if (!ok) {
        const ggui::Form* f = app.dialogs().current();
        ctx->LogError("dialog '%s' did not open (current: '%s')", title, f ? f->title.c_str() : "none");
    }
    ctx->Yield(2);
    return ok;
}

void Scenario::setText(const std::string& ref, const std::string& text)
{
    ctx->ItemClick(ref.c_str());
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
    ctx->KeyPress(ImGuiKey_Delete);
    if (!text.empty())
        ctx->KeyChars(text.c_str());
    ctx->Yield();
}

void Scenario::dialogText(const char* title, const char* field, const std::string& text)
{
    setText(std::string("//") + title + "/##" + field, text);
}

void Scenario::dialogCheck(const char* title, const char* field, const char* label, bool on)
{
    const std::string ref = std::string("//") + title + "/" + label + "##" + field;
    if (on)
        ctx->ItemCheck(ref.c_str());
    else
        ctx->ItemUncheck(ref.c_str());
}

void Scenario::dialogButton(const char* title, const char* button)
{
    ctx->ItemClick((std::string("//") + title + "/" + button).c_str());
    ctx->Yield(2);
}

bool Scenario::settle(float seconds) { return waitIdle(seconds); }

std::vector<Scenario::DrawnGlyph> Scenario::drawnGlyphs(ImGuiWindow* w)
{
    struct Key {
        float u, v;
        bool operator<(const Key& o) const { return u < o.u || (u == o.u && v < o.v); }
    };
    std::map<Key, std::pair<float, unsigned>> glyphs; // uv0 -> (glyph top - baseline, codepoint)
    for (ImFont* font : {ggui::theme().uiFont(), ggui::theme().monoFont()}) {
        ImFontBaked* baked = font->GetFontBaked(ImGui::GetStyle().FontSizeBase * ImGui::GetStyle().FontScaleMain);
        for (const ImFontGlyph& g : baked->Glyphs)
            if (g.Visible)
                glyphs[{g.U0, g.V0}] = {g.Y0 - baked->Ascent, g.Codepoint};
    }
    std::vector<DrawnGlyph> found;
    if (!w || !w->WasActive || w->Hidden || !w->DrawList)
        return found;
    const auto& vb = w->DrawList->VtxBuffer;
    for (int i = 0; i + 3 < vb.Size; ++i) {
        const auto it = glyphs.find({vb[i].uv.x, vb[i].uv.y});
        if (it == glyphs.end())
            continue;
        // PrimRectUV order: a, (c.x, a.y), c, (a.x, c.y).
        if (vb[i + 1].pos.y != vb[i].pos.y || vb[i + 3].pos.x != vb[i].pos.x || vb[i + 2].pos.x != vb[i + 1].pos.x)
            continue;
        found.push_back({vb[i].pos.y - it->second.first, vb[i].pos.x, it->second.second, vb[i].col});
        i += 3;
    }
    return found;
}

namespace {

// Glyphs of a window and its child windows (tables with scrolling, child regions), in reading order.
std::vector<Scenario::DrawnGlyph> windowGlyphs(ImGuiTestContext* ctx, const char* windowRef)
{
    ctx->Yield(1);
    std::vector<Scenario::DrawnGlyph> glyphs;
    if (ImGuiWindow* root = ctx->GetWindowByRef(windowRef))
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            for (ImGuiWindow* p = w; p; p = p->ParentWindow)
                if (p == root) {
                    const auto more = Scenario::drawnGlyphs(w);
                    glyphs.insert(glyphs.end(), more.begin(), more.end());
                    break;
                }
    std::sort(glyphs.begin(), glyphs.end(), [](const Scenario::DrawnGlyph& a, const Scenario::DrawnGlyph& b) {
        return std::abs(a.baseline - b.baseline) > 0.5f ? a.baseline < b.baseline : a.x < b.x;
    });
    return glyphs;
}

} // namespace

bool Scenario::itemDrawsBackground(const char* ref)
{
    ctx->Yield(2);
    const ImGuiTestItemInfo info = ctx->ItemInfo(ref, ImGuiTestOpFlags_NoError);
    if (!info.ID || !info.Window || !info.Window->DrawList)
        return false;
    const ImRect item = info.RectFull;
    const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    const auto& vb = info.Window->DrawList->VtxBuffer;
    // Runs of untextured vertices of one colour are one filled shape; one that covers the item
    // but is not much taller than it (window and panel backgrounds are) is a highlight or frame.
    for (int i = 0; i < vb.Size;) {
        int j = i;
        ImRect box(vb[i].pos, vb[i].pos);
        while (j < vb.Size && vb[j].uv.x == white.x && vb[j].uv.y == white.y && vb[j].col == vb[i].col)
            box.Add(vb[j++].pos);
        if (j - i >= 4 && (vb[i].col >> IM_COL32_A_SHIFT) != 0 && box.Contains(item.GetCenter())
            && box.GetWidth() >= item.GetWidth() * 0.9f && box.GetHeight() < item.GetHeight() * 1.8f)
            return true;
        i = std::max(j, i + 1);
    }
    return false;
}

bool Scenario::idShownDimmed(const char* windowRef, const std::string& hex, size_t shortLen)
{
    const auto glyphs = windowGlyphs(ctx, windowRef);
    const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text), dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    // Lines as text (no spaces) with one colour per character.
    size_t start = 0;
    for (size_t i = 1; i <= glyphs.size(); ++i) {
        if (i < glyphs.size() && std::abs(glyphs[i].baseline - glyphs[start].baseline) <= 0.5f)
            continue;
        std::string text;
        for (size_t k = start; k < i; ++k)
            text += glyphs[k].codepoint < 0x80 ? static_cast<char>(glyphs[k].codepoint) : '?';
        const size_t at = text.find(hex);
        if (at != std::string::npos) {
            for (size_t k = 0; k < hex.size(); ++k)
                if (glyphs[start + at + k].col != (k < shortLen ? normal : dim)) {
                    ctx->LogInfo("ID %s: character %zu has the wrong colour", hex.c_str(), k);
                    return false;
                }
            return true;
        }
        start = i;
    }
    ctx->LogInfo("ID %s not drawn in %s", hex.c_str(), windowRef);
    return false;
}

std::vector<std::string> Scenario::drawnText(const char* windowRef)
{
    const auto glyphs = windowGlyphs(ctx, windowRef);
    std::vector<std::string> lines;
    float current = -1e9f, lastX = 0.0f;
    const float space = ImGui::GetFontSize() * 0.45f;
    for (const auto& g : glyphs) {
        if (std::abs(g.baseline - current) > 0.5f) {
            lines.emplace_back();
            current = g.baseline;
        } else if (g.x - lastX > space * 2.2f) {
            lines.back() += ' '; // spaces draw no glyph: re-insert one per gap
        }
        char buf[5] = {};
        ImTextCharToUtf8(buf, g.codepoint);
        lines.back() += g.codepoint < 0xE000 ? std::string(buf) : std::string("<icon>");
        lastX = g.x;
    }
    return lines;
}

bool Scenario::textShown(const char* windowRef, const std::string& text)
{
    // Compare without spaces (the draw list does not record them).
    auto squash = [](std::string t) {
        t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
        return t;
    };
    const std::string want = squash(text);
    const auto lines = drawnText(windowRef);
    for (const auto& line : lines)
        if (squash(line).find(want) != std::string::npos)
            return true;
    std::string shown;
    for (const auto& line : lines)
        shown += "\n    " + line;
    ctx->LogInfo("text '%s' not drawn in %s; drawn:%s", text.c_str(), windowRef, shown.c_str());
    return false;
}

bool Scenario::dismissError(float seconds)
{
    if (!waitUntil([&] { const ggui::Form* f = app.dialogs().current(); return f && f->icon; }, seconds)) {
        ctx->LogError("no error popup appeared");
        return false;
    }
    const std::string title = app.dialogs().current()->title;
    if (!dialogOpen(title.c_str()))
        return false;
    dialogButton(title.c_str(), "OK");
    return waitUntil([&] { const ggui::Form* f = app.dialogs().current(); return !f || f->title != title; }, 5.0f);
}

bool Scenario::expandMerge(const std::string& mergeHex)
{
    auto& history = session()->history();
    const auto id = ggui::core::Oid::fromHex(mergeHex);
    if (!waitUntil([&] { return history.row(id) != nullptr && !history.loading(); }))
        return false;
    if (!history.row(id)->collapsed)
        return true;
    ctx->ItemClick(("//History/**/" + mergeHex + "/##merge_icon").c_str());
    return waitUntil([&] { return history.row(id) && !history.row(id)->collapsed && !history.loading(); });
}

void Scenario::screenshot(const std::string& name)
{
    const fs::path dir = artifactsDir() / "screens";
    std::error_code ec;
    fs::create_directories(dir, ec);
    ctx->Yield(2);
    ctx->CaptureReset();
    const std::string png = (dir / (name + ".png")).string();
    ImStrncpy(ctx->CaptureArgs->InOutputFile, png.c_str(), IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
    ctx->CaptureScreenshot(ImGuiCaptureFlags_HideMouseCursor);
}

void Scenario::showPanel(const char* name)
{
    // Hidden panels (Reflog, Operations and Blame by default) are opened through View.
    const auto& panels = app.settings().data().panels;
    const auto it = panels.find(name);
    if (it == panels.end() ? !ggui::panel::defaultVisible(name) : !it->second) {
        ctx->MenuClick((std::string("//##MainMenuBar/View/") + name).c_str());
        ctx->Yield(3);
    }
    ctx->WindowFocus((std::string("//") + name).c_str());
    ctx->Yield(2);
}

std::string Scenario::child(const char* parent, const char* childName)
{
    ImGuiTestItemInfo info = ctx->WindowInfo((std::string(parent) + "/" + childName).c_str(), ImGuiTestOpFlags_NoError);
    if (!info.Window)
        return std::string(parent) + "/" + childName;
    return "//" + escapeRef(info.Window->Name);
}

std::string Scenario::escapeRef(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '/' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

gg::RunResult Scenario::runGgui(std::vector<std::string> args,
    std::vector<std::pair<std::string, std::optional<std::string>>> env)
{
    gg::RunRequest r;
#ifdef _WIN32
    r.args.push_back((fs::path(ggui::executableDir()) / "ggui.exe").string());
#else
    r.args.push_back((fs::path(ggui::executableDir()) / "ggui").string());
#endif
    for (auto& a : args)
        r.args.push_back(std::move(a));
    r.cwd = m_root;
    r.env = std::move(env);
    r.env.emplace_back("GGUI_HEADLESS", "1");
    r.cLocale = false;
    return gg::run(r);
}

namespace {

#ifdef _WIN32
// Git for Windows' (or MSYS2's) sh.exe: on PATH, else next to git.
fs::path shellProgram()
{
    static const fs::path sh = [] {
        if (fs::path p = gg::findInPath("sh"); !p.empty())
            return p;
        const fs::path git = gg::findInPath("git");
        std::error_code ec;
        for (fs::path dir = git.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path())
            if (fs::is_regular_file(dir / "usr" / "bin" / "sh.exe", ec))
                return dir / "usr" / "bin" / "sh.exe";
        return fs::path("sh.exe");
    }();
    return sh;
}
#endif

} // namespace

fs::path Scenario::writeTool(const fs::path& dir, const std::string& name, const std::string& script)
{
    fs::create_directories(dir);
    {
        std::ofstream f(dir / name, std::ios::binary);
        f << script;
    }
    fs::permissions(dir / name, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
#ifdef _WIN32
    const fs::path sh = shellProgram();
    std::ofstream cmd(dir / (name + ".cmd"), std::ios::binary);
    cmd << "@echo off\r\nset \"PATH=" << sh.parent_path().string() << ";%PATH%\"\r\n\"" << sh.string() << "\" \""
        << (dir / name).generic_string() << "\" %*\r\nexit /b %ERRORLEVEL%\r\n";
    return dir / (name + ".cmd");
#else
    return dir / name;
#endif
}

fs::path Scenario::toolPath(const std::string& name) const
{
#ifdef _WIN32
    return m_root / "fake-bin" / (name + ".cmd");
#else
    return m_root / "fake-bin" / name;
#endif
}

fs::path Scenario::fakeTool(const std::string& name, const std::string& body)
{
    const fs::path dir = m_root / "fake-bin";
    const fs::path log = m_root / (name + ".log");
    writeTool(dir, name, "#!/bin/sh\nfor a in \"$@\"; do printf '%s\\n' \"$a\" >> '" + log.generic_string() + "'; done\n" + body);
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const std::string path = ggui::getEnv("PATH");
    if (path.rfind(dir.string(), 0) != 0)
        ggui::setEnv("PATH", dir.string() + sep + path);
    return log;
}

void Scenario::comboSelect(const char* combo, const char* item)
{
    ctx->ItemClick(combo);
    ImGuiWindow* popup = ctx->GetWindowByRef("//$FOCUSED");
    IM_CHECK_SILENT(popup != nullptr);
    // Match by visible label: the "**/" wildcard cannot find labels that contain '/'.
    ImGuiTestItemList items;
    ctx->GatherItems(&items, ImGuiTestRef(popup->ID), 4);
    for (const ImGuiTestItemInfo& info : items) {
        std::string label = info.DebugLabel;
        if (const size_t hash = label.find("##"); hash != std::string::npos)
            label.resize(hash);
        if (label == item) {
            ctx->ItemClick(info.ID);
            return;
        }
    }
    ctx->ItemClick(ImGuiTestRef(("//" + std::string(popup->Name) + "/**/" + item).c_str()));
}

void Scenario::contextMenu(const char* ref, const char* path, bool shift)
{
    ctx->ItemClick(ref, ImGuiMouseButton_Right);
    if (shift) {
        ctx->KeyDown(ImGuiMod_Shift);
        ctx->Yield(2); // the menu swaps its items while Shift is held
    }
    ctx->MenuClick(("//$FOCUSED/" + std::string(path)).c_str());
    if (shift)
        ctx->KeyUp(ImGuiMod_Shift);
}

std::string Scenario::itemLabel(const char* ref)
{
    ImGuiTestItemInfo info = ctx->ItemInfo(ref, ImGuiTestOpFlags_NoError);
    return info.ID ? std::string(info.DebugLabel) : std::string();
}

std::string Scenario::clipboard()
{
    const char* text = ImGui::GetClipboardText();
    return text ? text : "";
}

} // namespace ggtest

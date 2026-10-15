#include "platform/SelfUpdate.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace ggui::update {

namespace {

constexpr std::string_view kUrlPrefix = "https://github.com/rokups/GoodGit/releases/download/";
constexpr std::string_view kDigestPrefix = "sha256:";

std::string stringField(const nlohmann::json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isCommitId(std::string_view text)
{
    return text.size() == 40 && std::all_of(text.begin(), text.end(), isHex);
}

bool isLowerHexDigest(std::string_view text)
{
    return text.size() == 64 &&
           std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

bool parseRelease(std::string_view json, Release& out, std::string& error)
{
    error.clear();
    out = Release{};
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        error = "The release data is not valid JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "The release data is not a JSON object";
        return false;
    }
    out.commit = stringField(root, "target_commitish");
    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array()) {
        return true;
    }
    for (const nlohmann::json& item : *assets) {
        if (!item.is_object() || stringField(item, "state") != "uploaded") {
            continue;
        }
        Asset asset;
        asset.name = stringField(item, "name");
        asset.url = stringField(item, "browser_download_url");
        const std::string digest = stringField(item, "digest");
        if (startsWith(digest, kDigestPrefix)) {
            asset.sha256 = digest.substr(kDigestPrefix.size());
        }
        const auto size = item.find("size");
        if (size != item.end() && size->is_number_unsigned()) {
            asset.size = size->get<std::uint64_t>();
        }
        out.assets.push_back(std::move(asset));
    }
    return true;
}

const Asset* selectAsset(const Release& release, std::string_view packageSystem)
{
    if (packageSystem.empty()) {
        return nullptr;
    }
    const std::string suffix =
        "-" + std::string(packageSystem) + (startsWith(packageSystem, "windows-") ? ".zip" : ".tar.gz");
    for (const Asset& asset : release.assets) {
        if (!startsWith(asset.name, "ggui-") || !endsWith(asset.name, suffix)) {
            continue;
        }
        if (!startsWith(asset.url, kUrlPrefix) || !isLowerHexDigest(asset.sha256) || asset.size == 0) {
            return nullptr;
        }
        return &asset;
    }
    return nullptr;
}

Freshness compare(std::string_view local, std::string_view release)
{
    if (!isCommitId(local) || !isCommitId(release)) {
        return Freshness::Unknown;
    }
    const bool same = std::equal(local.begin(), local.end(), release.begin(),
                                 [](char a, char b) { return lower(a) == lower(b); });
    return same ? Freshness::UpToDate : Freshness::Newer;
}

} // namespace ggui::update

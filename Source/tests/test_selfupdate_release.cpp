// Self-update: the release model (platform/SelfUpdate.hpp).
#include "tests/Harness.hpp"
#include "platform/SelfUpdate.hpp"

#include <string>

namespace ggtest {

namespace {

const char* const kBase = "https://github.com/rokups/GoodGit/releases/download/latest/";
const std::string kDigest(64, 'a');

std::string assetJson(const std::string& name, const std::string& state = "uploaded",
                      const std::string& digest = "sha256:" + kDigest, const std::string& url = "",
                      int size = 1234)
{
    return R"({"name":")" + name + R"(","browser_download_url":")" + (url.empty() ? kBase + name : url) +
           R"(","size":)" + std::to_string(size) + R"(,"digest":")" + digest + R"(","state":")" + state + R"("})";
}

std::string releaseJson(const std::string& assets)
{
    return R"({"target_commitish":"0123456789abcdef0123456789abcdef01234567","assets":[)" + assets + "]}";
}

bool parse(const std::string& json, ggui::update::Release& release)
{
    std::string error;
    return ggui::update::parseRelease(json, release, error);
}

} // namespace

GG_TEST("selfupdate", "parseRelease reads the commit and the uploaded assets")
{
    ggui::update::Release release;
    GG_CHECK(parse(releaseJson(assetJson("ggui-1-linux-x86_64.tar.gz") + "," +
                               assetJson("ggui-1-windows-msvc.zip", "starter") + "," +
                               assetJson("ggui-1-windows-mingw.zip", "open")),
                   release));
    GG_CHECK_STR_EQ(release.commit, "0123456789abcdef0123456789abcdef01234567");
    GG_CHECK_EQ(release.assets.size(), static_cast<size_t>(1));
    GG_CHECK_STR_EQ(release.assets[0].name, "ggui-1-linux-x86_64.tar.gz");
    GG_CHECK_STR_EQ(release.assets[0].sha256, kDigest);
    GG_CHECK_EQ(release.assets[0].size, static_cast<std::uint64_t>(1234));
}

GG_TEST("selfupdate", "parseRelease leaves sha256 empty without the prefix")
{
    ggui::update::Release release;
    GG_CHECK(parse(releaseJson(assetJson("ggui-1-linux-x86_64.tar.gz", "uploaded", "md5:abc")), release));
    GG_CHECK_EQ(release.assets.size(), static_cast<size_t>(1));
    GG_CHECK(release.assets[0].sha256.empty());
}

GG_TEST("selfupdate", "parseRelease reports malformed JSON without throwing")
{
    ggui::update::Release release;
    std::string error;
    GG_CHECK(!ggui::update::parseRelease("{not json", release, error));
    GG_CHECK(!error.empty());
    error.clear();
    GG_CHECK(!ggui::update::parseRelease("", release, error));
    GG_CHECK(!error.empty());
    error.clear();
    GG_CHECK(!ggui::update::parseRelease("[1,2]", release, error));
    GG_CHECK(!error.empty());
    // Wrong field types are tolerated.
    GG_CHECK(parse(R"({"target_commitish":5,"assets":[{"name":3,"size":"x","state":"uploaded"},7]})", release));
}

GG_TEST("selfupdate", "selectAsset picks the asset for each package system")
{
    ggui::update::Release release;
    GG_CHECK(parse(releaseJson(assetJson("ggui-1-windows-mingw.zip") + "," + assetJson("ggui-1-windows-msvc.zip") + "," +
                               assetJson("ggui-1-linux-x86_64.tar.gz") + "," +
                               assetJson("ggui-1-linux-x86_64.deb") + "," + assetJson("ggui-1-linux-x86_64.zip")),
                   release));
    const ggui::update::Asset* mingw = ggui::update::selectAsset(release, "windows-mingw");
    const ggui::update::Asset* msvc = ggui::update::selectAsset(release, "windows-msvc");
    const ggui::update::Asset* linuxAsset = ggui::update::selectAsset(release, "linux-x86_64");
    GG_CHECK(mingw != nullptr && mingw->name == "ggui-1-windows-mingw.zip");
    GG_CHECK(msvc != nullptr && msvc->name == "ggui-1-windows-msvc.zip");
    GG_CHECK(linuxAsset != nullptr && linuxAsset->name == "ggui-1-linux-x86_64.zip");
    GG_CHECK(ggui::update::selectAsset(release, "linux-aarch64") == nullptr);
    GG_CHECK(ggui::update::selectAsset(release, "") == nullptr);
}

GG_TEST("selfupdate", "selectAsset ignores .deb and .tar.gz on Linux")
{
    ggui::update::Release release;
    GG_CHECK(parse(releaseJson(assetJson("ggui-1-linux-x86_64.deb") + "," + assetJson("ggui-1-linux-x86_64.tar.gz")),
                   release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
}

GG_TEST("selfupdate", "selectAsset refuses untrusted assets")
{
    const std::string name = "ggui-1-linux-x86_64.zip";
    ggui::update::Release release;
    // Bad digest: wrong length, uppercase, missing.
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "sha256:abc")), release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "sha256:" + std::string(64, 'A'))), release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "")), release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    // Plain http and a foreign repository.
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "sha256:" + kDigest,
                                         "http://github.com/rokups/GoodGit/releases/download/latest/" + name)),
                   release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "sha256:" + kDigest,
                                         "https://github.com/evil/GoodGit/releases/download/latest/" + name)),
                   release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    // Zero size.
    GG_CHECK(parse(releaseJson(assetJson(name, "uploaded", "sha256:" + kDigest, "", 0)), release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") == nullptr);
    // A good asset is accepted.
    GG_CHECK(parse(releaseJson(assetJson(name)), release));
    GG_CHECK(ggui::update::selectAsset(release, "linux-x86_64") != nullptr);
}

GG_TEST("selfupdate", "compare tells whether the release is newer")
{
    using ggui::update::Freshness;
    const std::string a = "0123456789abcdef0123456789abcdef01234567";
    const std::string b = "fedcba9876543210fedcba9876543210fedcba98";
    GG_CHECK(ggui::update::compare(a, a) == Freshness::UpToDate);
    GG_CHECK(ggui::update::compare("0123456789ABCDEF0123456789ABCDEF01234567", a) == Freshness::UpToDate);
    GG_CHECK(ggui::update::compare(a, b) == Freshness::Newer);
    GG_CHECK(ggui::update::compare("", a) == Freshness::Unknown);
    GG_CHECK(ggui::update::compare(a, "") == Freshness::Unknown);
    GG_CHECK(ggui::update::compare("abc", "abc") == Freshness::Unknown);
    GG_CHECK(ggui::update::compare(std::string(40, 'g'), a) == Freshness::Unknown);
}

} // namespace ggtest

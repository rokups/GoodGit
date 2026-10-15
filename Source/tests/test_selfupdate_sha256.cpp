// Self-update: SHA-256 (util/Sha256.hpp).
#include "tests/Harness.hpp"
#include "util/Sha256.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace ggtest {

GG_TEST("selfupdate", "sha256Hex matches the standard test vectors")
{
    GG_CHECK_STR_EQ(ggui::sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    GG_CHECK_STR_EQ(ggui::sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    GG_CHECK_STR_EQ(ggui::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    GG_CHECK_STR_EQ(ggui::sha256Hex(std::string(1000000, 'a')),
                    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

GG_TEST("selfupdate", "sha256File matches sha256Hex and reports a missing file")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ggui-sha256-test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "data.bin";
    // Larger than one 64 KiB read block, so the streaming path runs more than once.
    const std::string content(200000, 'q');
    {
        std::ofstream out(file, std::ios::binary);
        out << content;
    }
    std::string error;
    GG_CHECK_STR_EQ(ggui::sha256File(file, error), ggui::sha256Hex(content));
    GG_CHECK(error.empty());

    error.clear();
    const std::string missing = ggui::sha256File(dir / "missing.bin", error);
    GG_CHECK(missing.empty());
    GG_CHECK(!error.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace ggtest

// Self-update: SHA-256 (util/Sha256.hpp).
#include "tests/Harness.hpp"
#include "tests/SelfUpdateHelpers.hpp"
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

GG_TEST("selfupdate", "sha256Hex handles the padding boundary sizes")
{
    // Digests come from sha256sum of 'a' repeated N times.
    struct Case {
        std::size_t size;
        const char* digest;
    };
    const Case cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    };
    for (const Case& c : cases)
        GG_CHECK_STR_EQ(ggui::sha256Hex(std::string(c.size, 'a')), c.digest);
}

GG_TEST("selfupdate", "sha256File matches sha256Hex around the 64 KiB block size")
{
    // The incremental hasher is private. Uneven file sizes give uneven update calls.
    const suhelp::TempRoot root;
    const std::filesystem::path file = root.path / "edge.bin";
    for (const std::size_t size : {1u, 63u, 65u, 65535u, 65536u, 65537u, 131073u}) {
        std::string content(size, '\0');
        for (std::size_t i = 0; i < size; ++i)
            content[i] = static_cast<char>(i * 31 + 7);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << content;
        }
        std::string error;
        GG_CHECK_STR_EQ(ggui::sha256File(file, error), ggui::sha256Hex(content));
        GG_CHECK(error.empty());
    }
}

GG_TEST("selfupdate", "sha256File matches sha256Hex and reports a missing file")
{
    const suhelp::TempRoot root;
    const std::filesystem::path dir = root.path;
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
}

} // namespace ggtest

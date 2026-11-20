// Picker threads must not post to a closed App (GG-64). The gate is tested on its own,
// because the test harness cannot shut the App down inside a test.
#include "tests/Harness.hpp"
#include "util/PickerGate.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace ggtest {

GG_TEST("pickergate", "a late picker result is dropped after close")
{
    auto gate = std::make_shared<ggui::PickerGate>();
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};
    std::atomic<int> posted{0};
    // The fake picker blocks like a native dialog, then posts its result.
    std::thread picker([gate, &started, &release, &posted] {
        started = true;
        while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        gate->post([&posted] { ++posted; });
    });
    while (!started)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate->close(); // the App shuts down while the dialog is open
    release = true;
    picker.join();
    GG_CHECK(posted == 0);
}

GG_TEST("pickergate", "an open gate runs the post")
{
    ggui::PickerGate gate;
    int posted = 0;
    GG_CHECK(gate.post([&posted] { ++posted; }));
    GG_CHECK(posted == 1);
    gate.close();
    GG_CHECK(!gate.post([&posted] { ++posted; }));
    GG_CHECK(posted == 1);
}

} // namespace ggtest

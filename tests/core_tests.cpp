#include "core/file_monitor.hpp"
#include "core/hotkey.hpp"
#include "core/logger.hpp"
#include "core/notifications.hpp"
#include "core/reload_manager.hpp"
#include "core/json_ui.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

void write(const fs::path& file, const char* contents) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << contents;
    require(static_cast<bool>(stream), "Cannot write fixture");
}

struct TempDirectory {
    fs::path path = fs::temp_directory_path() / ("conduit-core-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { fs::create_directory(path); }
    ~TempDirectory() { std::error_code error; fs::remove_all(path, error); }
};

void testMonitor() {
    TempDirectory temp;
    const auto ui = temp.path / "ui";
    fs::create_directories(ui / "nested");
    const auto first = ui / "screen.json";
    write(first, "{\"a\":1}");
    conduit::FileMonitor monitor(ui, 100ms);
    auto now = conduit::FileMonitor::Clock::now();
    require(monitor.poll(now).empty(), "Initial files must only establish baseline");
    write(ui / "ignored.txt", "not JSON");
    require(monitor.poll(now + 1ms).empty(), "Non-JSON files should not trigger reload");
    const auto timestamp = fs::last_write_time(first);
    write(first, "{\"a\":2}");
    fs::last_write_time(first, timestamp);
    require(monitor.poll(now + 10ms).empty(), "Changes must be debounced");
    write(first, "{\"a\":3}");
    fs::last_write_time(first, timestamp);
    require(monitor.poll(now + 80ms).empty(), "An edit burst must restart debounce");
    require(monitor.poll(now + 150ms).empty(), "An edit burst must settle first");
    auto changes = monitor.poll(now + 190ms);
    require(changes.size() == 1 && changes[0].kind == conduit::ChangeKind::Modified,
            "Same-size edits with preserved timestamps must be detected");
    require(monitor.poll(now + 300ms).empty(), "Stable content must not repeat events");

    write(ui / "nested" / "new.JSON", "{}");
    fs::remove(first);
    require(monitor.poll(now + 310ms).empty(), "Creation/deletion must debounce");
    changes = monitor.poll(now + 420ms);
    require(changes.size() == 2, "Recursive create and delete events missing");
    bool created = false;
    bool removed = false;
    for (const auto& change : changes) {
        created |= change.kind == conduit::ChangeKind::Created;
        removed |= change.kind == conduit::ChangeKind::Removed;
    }
    require(created && removed, "Wrong change kinds");

    fs::rename(ui, temp.path / "away");
    bool failed = false;
    try { (void)monitor.poll(now + 500ms); } catch (const std::exception&) { failed = true; }
    require(failed, "Missing directory must report an error, not publish deletion events");
    fs::rename(temp.path / "away", ui);
    require(monitor.poll(now + 700ms).empty(), "Scan failure must preserve the snapshot");
}

void testLogger() {
    TempDirectory temp;
    const auto path = temp.path / "test.log";
    {
        conduit::Logger logger(path);
        std::vector<std::jthread> workers;
        for (int thread = 0; thread < 8; ++thread) {
            workers.emplace_back([&, thread] {
                for (int line = 0; line < 100; ++line) {
                    logger.write("test", "entry-" + std::to_string(thread) + ":" + std::to_string(line));
                }
            });
        }
        workers.clear();
        logger.write("test", "one\ntwo\rthree");
        require(!logger.failed(), "Logger reported a write error");
    }
    std::ifstream stream(path);
    std::set<std::string> entries;
    std::string line;
    while (std::getline(stream, line)) {
        const auto marker = line.find("[test] ");
        require(marker != std::string::npos, "Interleaved or malformed log record");
        entries.insert(line.substr(marker + 7));
    }
    require(entries.size() == 801, "Concurrent log records were lost or interleaved");
    require(entries.contains("one two three"), "Multiline message was not normalized");
}

void testReloadAndHotkey() {
    conduit::RisingEdge chordEdge;
    require(!chordEdge.update(false), "A released chord must not trigger");
    require(chordEdge.update(true), "A chord press must trigger");
    require(!chordEdge.update(true), "A held chord must not trigger repeatedly");
    require(!chordEdge.update(false), "A chord release must not trigger");
    require(chordEdge.update(true), "A second chord press must trigger");
    conduit::RisingEdge edge;
    require(!edge.update(false) && edge.update(true) && !edge.update(true), "Hotkey repeated while held");
    require(!edge.update(false) && edge.update(true), "Hotkey did not re-arm after release");
    conduit::RisingEdge held(true);
    require(!held.update(true), "Held key on startup must not trigger");

    conduit::ReloadManager manager(std::make_unique<conduit::MinecraftReloadBackend>());
    require(manager.reload({}) == conduit::ReloadResult::Unsupported,
            "Placeholder backend must never claim success");
    struct TestBackend final : conduit::ReloadBackend {
        conduit::ReloadResult reload(const conduit::ReloadRequest& request) override {
            return request.explicitRequest ? conduit::ReloadResult::Succeeded : conduit::ReloadResult::Failed;
        }
    };
    conduit::ReloadManager testManager(std::make_unique<TestBackend>());
    require(testManager.reload({true, false, {}}) == conduit::ReloadResult::Succeeded,
            "Manager must forward the request to its backend");
}

void testNotifications() {
    conduit::NotificationCenter center(2);
    const auto now = std::chrono::steady_clock::now();
    center.push("Conduit Client", "Successfully injected!", now);
    center.push("Conduit Client", "JSON UI reload requested", now + 10ms);
    center.push("Conduit Client", "Unloading client...", now + 20ms);
    const auto visible = center.snapshot(now + 20ms);
    require(visible.size() == 2, "Notification queue did not enforce capacity");
    require(visible[0].description == "JSON UI reload requested" &&
            visible[1].description == "Unloading client...", "Notification order is wrong");
    require(center.snapshot(now + 4s).empty(), "Expired notification was retained");
    center.setDebugPanels({{"JSON UI", "Attempts 12", {}}});
    require(center.snapshot(now + 10s).size() == 1, "Debug panels must persist");
    center.push("Toast", "Reload requested", now + 10s);
    require(center.snapshot(now + 10s).size() == 2, "Debug panel displaced toast");
    center.setDebugPanels({});
    require(center.snapshot(now + 10s).size() == 1, "Hiding diagnostics removed toast");
}

void testExperimentalMetadataScan() {
    TempDirectory temp;
    fs::create_directories(temp.path / ".git" / "nested");
    fs::create_directories(temp.path / "@example" / "interfaces");
    write(temp.path / "@example" / "interfaces" / "screen.json", R"({"namespace":"test","screen":{}})");
    write(temp.path / ".git" / "nested" / "broken.json", "invalid metadata");
    conduit::JsonUiRepository repository;
    require(!repository.refresh(temp.path), "Default scan behavior changed");
    require(repository.refresh(temp.path, true) && repository.info().files == 1,
            "Experimental scan must validate custom definitions and skip metadata");
    conduit::FileMonitor monitor(temp.path, 0ms, true);
    write(temp.path / ".git" / "nested" / "broken.json", "changed metadata");
    require(monitor.poll(conduit::FileMonitor::Clock::now()).empty(), "Metadata caused experimental reload");
    write(temp.path / "@example" / "interfaces" / "screen.json", "invalid UI");
    require(!monitor.poll(conduit::FileMonitor::Clock::now()).empty(), "Custom definition change missed");
    require(!repository.refresh(temp.path, true), "Invalid custom UI was accepted");
}

void testJsonUi() {
    TempDirectory temp;
    conduit::JsonUiRepository repository;
    write(temp.path / "screen.json", R"({/* comment */"namespace":"example","screen":{"type":"panel"}})");
    require(repository.refresh(temp.path), "Valid JSON UI was rejected");
    const auto good = repository.info();
    require(good.files == 1 && good.definitions == 1 &&
            good.namespaces == std::vector<std::string>{"example"}, "Incorrect JSON UI metadata");
    write(temp.path / "broken.json", "{\"namespace\":");
    require(!repository.refresh(temp.path), "Invalid JSON UI was accepted");
    const auto failed = repository.info();
    require(failed.files == good.files && failed.definitions == good.definitions &&
            failed.namespaces == good.namespaces, "Failed parse replaced known good metadata");
    require(failed.error.find("broken.json") != std::string::npos, "Parse error lost file context");
    write(temp.path / "broken.json", R"({"namespace":12})");
    require(!repository.refresh(temp.path), "Non-string namespace was accepted");
    fs::remove(temp.path / "broken.json");
    require(repository.refresh(temp.path) && repository.info().error.empty(), "Validation did not recover");
    fs::remove(temp.path / "screen.json");
    require(repository.refresh(temp.path) && repository.info().files == 0, "Deleted file was retained");
}
}

int main() {
    try {
        testMonitor();
        testLogger();
        testReloadAndHotkey();
        testNotifications();
        testJsonUi();
        testExperimentalMetadataScan();
        std::cout << "Core tests passed: debounce, content changes, scan recovery, logging, hotkeys, backend\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

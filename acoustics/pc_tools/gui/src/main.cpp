#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "misc/cpp/imgui_stdlib.h"

#include "acoustics/common/DeviceRegistry.h"
#include "acoustics/osc/OscTransport.h"
#include "acoustics/scheduler/SoundTimeline.h"

#include "json.hpp"

#include <asio/ip/address.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::chrono::milliseconds kRegistryRefreshInterval{500};
constexpr double kLatencyWarningMs = 100.0;
constexpr double kLatencyCriticalMs = 250.0;
constexpr double kHeartbeatWarningSeconds = 3.0;
constexpr double kHeartbeatCriticalSeconds = 10.0;
constexpr std::size_t kMaxLogEntries = 300;
const fs::path kDefaultEventLogCsv{"logs/gui_event_log.csv"};

struct EventLogEntry {
    std::chrono::system_clock::time_point timestamp;
    spdlog::level::level_enum level{spdlog::level::info};
    std::string message;
};

void trimLog(std::deque<EventLogEntry>& log);

enum class DeviceHealth {
    Ok,
    Warning,
    Critical
};

struct DeviceSummary {
    acoustics::common::DeviceSnapshot snapshot;
    std::string alias;
    double meanLatency{0.0};
    double stdLatency{0.0};
    double secondsSinceSeen{0.0};
    DeviceHealth health{DeviceHealth::Critical};
};

class AliasStore {
public:
    explicit AliasStore(fs::path path) : path_(std::move(path)) {
        ensureParentExists();
        load();
    }

    std::string aliasFor(const std::string& deviceId) const {
        if (auto it = aliases_.find(deviceId); it != aliases_.end()) {
            return it->second;
        }
        return {};
    }

    void setAlias(const std::string& deviceId, const std::string& alias) {
        if (alias.empty()) {
            aliases_.erase(deviceId);
        } else {
            aliases_[deviceId] = alias;
        }
        save();
    }

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
    std::unordered_map<std::string, std::string> aliases_;

    void ensureParentExists() {
        if (path_.has_parent_path()) {
            fs::create_directories(path_.parent_path());
        }
    }

    void load() {
        aliases_.clear();
        if (!fs::exists(path_)) {
            return;
        }
        std::ifstream in(path_);
        if (!in) {
            spdlog::warn("Failed to open alias store: {}", path_.string());
            return;
        }
        json data;
        try {
            in >> data;
            if (data.is_object()) {
                for (auto it = data.begin(); it != data.end(); ++it) {
                    if (it.value().is_string()) {
                        aliases_[it.key()] = it.value().get<std::string>();
                    }
                }
            }
        } catch (const std::exception& ex) {
            spdlog::error("Alias store parse error: {}", ex.what());
        }
    }

    void save() const {
        std::ofstream out(path_);
        if (!out) {
            spdlog::error("Failed to write alias store: {}", path_.string());
            return;
        }
        json data(json::value_t::object);
        for (const auto& [id, alias] : aliases_) {
            data[id] = alias;
        }
        out << data.dump(2);
    }
};

struct OscConfig {
    std::string host{"192.168.2.255"};
    int port{9000};
    bool broadcast{true};
};

class OscController {
public:
    OscController()
        : ioContext_(),
          endpoint_(asio::ip::make_address("192.168.2.255"), 9000),
          sender_(ioContext_, endpoint_, true) {}

    void updateConfig(const OscConfig& cfg, std::deque<EventLogEntry>& log) {
        try {
            asio::ip::address address = asio::ip::make_address(cfg.host);
            endpoint_ = acoustics::osc::OscSender::Endpoint(address, static_cast<unsigned short>(cfg.port));
            sender_.setEndpoint(endpoint_);
            sender_.setBroadcastEnabled(cfg.broadcast);
            log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::info,
                                           fmt::format("OSC endpoint set to {}:{} (broadcast={})", cfg.host, cfg.port, cfg.broadcast)});
        } catch (const std::exception& ex) {
            log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                           fmt::format("Failed to apply OSC endpoint: {}", ex.what())});
        }
        trimLog(log);
    }

    bool sendMessage(const acoustics::osc::Message& msg, std::deque<EventLogEntry>& log) {
        try {
            sender_.send(msg);
            return true;
        } catch (const std::exception& ex) {
            log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                           fmt::format("OSC send failed: {}", ex.what())});
            trimLog(log);
            return false;
        }
    }

    bool sendBundle(const acoustics::osc::Bundle& bundle, std::deque<EventLogEntry>& log) {
        try {
            sender_.send(bundle);
            return true;
        } catch (const std::exception& ex) {
            log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                           fmt::format("OSC bundle send failed: {}", ex.what())});
            trimLog(log);
            return false;
        }
    }

private:
    asio::io_context ioContext_;
    acoustics::osc::OscSender::Endpoint endpoint_;
    acoustics::osc::OscSender sender_;
};

std::string formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    return std::string(buffer);
}

DeviceHealth classifyHealth(double secondsSinceSeen, double meanLatency) {
    if (secondsSinceSeen > kHeartbeatCriticalSeconds) {
        return DeviceHealth::Critical;
    }
    if (secondsSinceSeen > kHeartbeatWarningSeconds) {
        return DeviceHealth::Warning;
    }
    if (meanLatency > kLatencyCriticalMs) {
        return DeviceHealth::Critical;
    }
    if (meanLatency > kLatencyWarningMs) {
        return DeviceHealth::Warning;
    }
    return DeviceHealth::Ok;
}

ImU32 colorForHealth(DeviceHealth health) {
    switch (health) {
        case DeviceHealth::Ok:
            return IM_COL32(76, 217, 100, 255);
        case DeviceHealth::Warning:
            return IM_COL32(255, 204, 0, 255);
        case DeviceHealth::Critical:
        default:
            return IM_COL32(255, 59, 48, 255);
    }
}

std::vector<DeviceSummary> buildDeviceSummaries(
    acoustics::common::DeviceRegistry& registry,
    AliasStore& aliases,
    std::chrono::steady_clock::time_point& lastRefresh,
    std::chrono::steady_clock::time_point now) {

    if (now - lastRefresh < kRegistryRefreshInterval) {
        return {};
    }
    lastRefresh = now;

    registry.load();
    auto snapshots = registry.snapshot();
    std::vector<DeviceSummary> result;
    result.reserve(snapshots.size());
    for (auto& snap : snapshots) {
        DeviceSummary summary;
        summary.snapshot = snap;
        summary.alias = aliases.aliasFor(snap.state.id);
        const auto& heartbeat = snap.state.heartbeat;
        summary.meanLatency = heartbeat.count > 0 ? heartbeat.meanLatencyMs : 0.0;
        summary.stdLatency = heartbeat.standardDeviation();
        summary.secondsSinceSeen = std::chrono::duration<double>(snap.snapshotTime - snap.state.lastSeen).count();
        summary.health = classifyHealth(summary.secondsSinceSeen, summary.meanLatency);
        result.push_back(std::move(summary));
    }
    std::sort(result.begin(), result.end(), [](const DeviceSummary& a, const DeviceSummary& b) {
        return a.snapshot.state.id < b.snapshot.state.id;
    });
    return result;
}

std::optional<std::chrono::system_clock::time_point> parseIso8601(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::string copy = value;
    if (!copy.empty() && copy.back() == 'Z') {
        copy.pop_back();
    }
    auto dotPos = copy.find('.');
    std::string fractional;
    if (dotPos != std::string::npos) {
        fractional = copy.substr(dotPos + 1);
        copy = copy.substr(0, dotPos);
    }
    std::tm tm{};
    std::istringstream iss(copy);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        return std::nullopt;
    }
    auto timeT =
#if defined(_WIN32)
        _mkgmtime(&tm);
#else
        timegm(&tm);
#endif
    std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(timeT);
    if (!fractional.empty()) {
        double fraction = std::stod("0." + fractional);
        tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>(fraction));
    }
    return tp;
}

void trimLog(std::deque<EventLogEntry>& log) {
    while (log.size() > kMaxLogEntries) {
        log.pop_front();
    }
}

std::string displayAlias(const DeviceSummary& summary) {
    if (!summary.alias.empty()) {
        return summary.alias;
    }
    return summary.snapshot.state.id;
}

void sendTestSignal(OscController& osc,
                    const std::string& preset,
                    const DeviceSummary& device,
                    double leadSeconds,
                    std::deque<EventLogEntry>& log) {
    acoustics::osc::Message msg;
    msg.address = "/acoustics/play";
    msg.arguments.emplace_back(preset);
    auto offsetMs = static_cast<std::int32_t>(leadSeconds * 1000.0);
    msg.arguments.emplace_back(offsetMs);
    msg.arguments.emplace_back(static_cast<float>(1.0f));
    msg.arguments.emplace_back(static_cast<std::int32_t>(0));

    if (osc.sendMessage(msg, log)) {
        log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::info,
                                       fmt::format("Test signal '{}' sent to {}", preset, device.snapshot.state.id)});
    }
    trimLog(log);
}

void sendTimelineToDevices(const std::vector<DeviceSummary>& devices,
                           const std::set<std::string>& selected,
                           const fs::path& timelinePath,
                           double leadSeconds,
                           bool baseNow,
                           const std::string& baseTimeString,
                           OscController& osc,
                           std::deque<EventLogEntry>& log) {
    if (!fs::exists(timelinePath)) {
        log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                       fmt::format("Timeline file not found: {}", timelinePath.string())});
        trimLog(log);
        return;
    }

    std::vector<const DeviceSummary*> targets;
    if (selected.empty()) {
        for (const auto& dev : devices) {
            targets.push_back(&dev);
        }
    } else {
        for (const auto& dev : devices) {
            if (selected.count(dev.snapshot.state.id)) {
                targets.push_back(&dev);
            }
        }
    }

    if (targets.empty()) {
        log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::warn,
                                       "No devices selected for timeline send."});
        trimLog(log);
        return;
    }

    try {
        auto timeline = acoustics::scheduler::SoundTimeline::fromJsonFile(timelinePath);
        std::chrono::system_clock::time_point baseTime = std::chrono::system_clock::now();
        if (!baseNow) {
            if (auto parsed = parseIso8601(baseTimeString)) {
                baseTime = *parsed;
            } else {
                log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::warn,
                                               "Failed to parse base time. Using now."});
            }
        }
        auto bundles = timeline.toBundles(baseTime, leadSeconds);
        std::size_t success = 0;
        for (auto bundle : bundles) {
            if (osc.sendBundle(bundle, log)) {
                ++success;
            }
        }
        log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::info,
                                       fmt::format("Timeline '{}' dispatched (targets=%zu, bundles=%zu)",
                                                   targets.size(), success)});
        trimLog(log);
    } catch (const std::exception& ex) {
        log.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                       fmt::format("Timeline send failed: {}", ex.what())});
        trimLog(log);
    }
}

} // namespace

int main() {
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Acoustics Monitor", nullptr, nullptr);
    if (!window) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    fs::path stateDir = fs::path("state");
    fs::create_directories(stateDir);
    fs::create_directories("logs");

    fs::path devicesPath = stateDir / "devices.json";
    fs::path aliasPath = stateDir / "device_aliases.json";

    acoustics::common::DeviceRegistry registry(devicesPath);
    AliasStore aliasStore(aliasPath);
    OscController oscController;

    OscConfig oscConfig{};
    std::optional<std::string> renamingId;
    std::string aliasEditBuffer;

    std::set<std::string> selectedDevices;
    std::deque<EventLogEntry> eventLog;

    std::chrono::steady_clock::time_point lastRefresh = std::chrono::steady_clock::now() - kRegistryRefreshInterval;
    std::vector<DeviceSummary> devices;

    char timelinePathBuffer[512] = {0};
    char baseTimeBuffer[64] = {0};
    std::strncpy(timelinePathBuffer, "acoustics/pc_tools/scheduler/examples/basic_timeline.json", sizeof(timelinePathBuffer) - 1);
    bool baseTimeNow = true;
    double leadTimeSeconds = 1.0;
    char testPresetBuffer[64] = {0};
    std::strncpy(testPresetBuffer, "test_ping", sizeof(testPresetBuffer) - 1);
    double testLeadSeconds = 0.5;

    char hostBuffer[128];
    std::strncpy(hostBuffer, oscConfig.host.c_str(), sizeof(hostBuffer) - 1);
    int portValue = oscConfig.port;

    oscController.updateConfig(oscConfig, eventLog);

    const bool hasSavedLayout = fs::exists("imgui.ini");
    bool dockspaceBuilt = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        auto refreshed = buildDeviceSummaries(registry, aliasStore, lastRefresh, now);
        if (!refreshed.empty()) {
            devices = std::move(refreshed);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        if (!dockspaceBuilt && !hasSavedLayout) {
            dockspaceBuilt = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.30f, nullptr, &dockMain);
            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

            ImGui::DockBuilderDockWindow("Dispatch", dockRight);
            ImGui::DockBuilderDockWindow("OSC Endpoint", dockRight);
            ImGui::DockBuilderDockWindow("Event Log", dockBottom);
            ImGui::DockBuilderDockWindow("Status", dockBottom);
            ImGui::DockBuilderDockWindow("Devices", dockMain);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        if (ImGui::Begin("OSC Endpoint")) {
            ImGui::InputText("Host", hostBuffer, IM_ARRAYSIZE(hostBuffer));
            ImGui::InputInt("Port", &portValue);
            ImGui::Checkbox("Broadcast", &oscConfig.broadcast);
            if (ImGui::Button("Apply")) {
                oscConfig.host = hostBuffer;
                oscConfig.port = std::clamp(portValue, 1, 65535);
                oscController.updateConfig(oscConfig, eventLog);
            }
        }
        ImGui::End();

        if (ImGui::Begin("Devices")) {
            ImGui::Text("Online: %zu", devices.size());
            const int tilesPerColumn = 20;
            int tileIndex = 0;
            ImGui::BeginChild("DeviceGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (auto& dev : devices) {
                if (tileIndex % tilesPerColumn == 0) {
                    if (tileIndex != 0) {
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                }

                ImGui::PushID(dev.snapshot.state.id.c_str());
                ImGui::BeginChild("DeviceTile", ImVec2(220, 135), true);
            std::string title = displayAlias(dev);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", title.c_str());
            ImGui::TextDisabled("%s", dev.snapshot.state.id.c_str());

            ImGui::SameLine(160.0f);
            ImGui::ColorButton("##status", ImColor(colorForHealth(dev.health)), ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));

            if (ImGui::Button("Rename")) {
                renamingId = dev.snapshot.state.id;
                aliasEditBuffer = dev.alias;
            }
            if (renamingId && *renamingId == dev.snapshot.state.id) {
                ImGui::InputText("Alias", &aliasEditBuffer);
                if (ImGui::Button("Save")) {
                    aliasStore.setAlias(dev.snapshot.state.id, aliasEditBuffer);
                    dev.alias = aliasEditBuffer;
                    eventLog.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::info,
                                                        fmt::format("Alias updated: {} => '{}'", dev.snapshot.state.id, aliasEditBuffer)});
                    trimLog(eventLog);
                    renamingId.reset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    renamingId.reset();
                }
            }

            bool selected = selectedDevices.count(dev.snapshot.state.id) > 0;
            if (ImGui::Checkbox("Select", &selected)) {
                if (selected) {
                    selectedDevices.insert(dev.snapshot.state.id);
                } else {
                    selectedDevices.erase(dev.snapshot.state.id);
                }
            }

            ImGui::Text("Latency: %.1f ms (std %.1f)", dev.meanLatency, dev.stdLatency);
            ImGui::Text("Heartbeat: %.1f s ago", dev.secondsSinceSeen);

            if (ImGui::Button("Test Signal")) {
                sendTestSignal(oscController, testPresetBuffer, dev, testLeadSeconds, eventLog);
            }
            ImGui::SameLine();
            if (ImGui::Button("Focus")) {
                selectedDevices.clear();
                selectedDevices.insert(dev.snapshot.state.id);
            }

            ImGui::EndChild();
            ImGui::PopID();

            ++tileIndex;
            if (tileIndex % tilesPerColumn == 0) {
                ImGui::EndGroup();
            }
        }
        if (tileIndex % tilesPerColumn != 0) {
            ImGui::EndGroup();
        }
        ImGui::EndChild();
        }
        ImGui::End();

        if (ImGui::Begin("Dispatch")) {
            ImGui::TextUnformatted("Selected Devices");
            if (selectedDevices.empty()) {
                ImGui::TextDisabled("(none)");
            } else {
                for (const auto& id : selectedDevices) {
                    ImGui::BulletText("%s", id.c_str());
                }
                if (ImGui::Button("Clear Selection")) {
                    selectedDevices.clear();
                }
            }

            ImGui::Separator();

            ImGui::InputText("Timeline", timelinePathBuffer, IM_ARRAYSIZE(timelinePathBuffer));
            ImGui::Checkbox("Use current time", &baseTimeNow);
            if (!baseTimeNow) {
                ImGui::InputText("Base time (ISO)", baseTimeBuffer, IM_ARRAYSIZE(baseTimeBuffer));
            }
            ImGui::SliderFloat("Lead time (s)", reinterpret_cast<float*>(&leadTimeSeconds), 0.0f, 5.0f);
            if (ImGui::Button("Send Timeline")) {
                sendTimelineToDevices(devices,
                                      selectedDevices,
                                      fs::path(timelinePathBuffer),
                                      leadTimeSeconds,
                                      baseTimeNow,
                                      baseTimeBuffer,
                                      oscController,
                                      eventLog);
            }

            ImGui::Separator();
            ImGui::InputText("Test preset", testPresetBuffer, IM_ARRAYSIZE(testPresetBuffer));
            ImGui::SliderFloat("Test lead (s)", reinterpret_cast<float*>(&testLeadSeconds), 0.0f, 2.0f);
        }
        ImGui::End();

        if (ImGui::Begin("Event Log")) {
            if (ImGui::Button("Export CSV")) {
                try {
                    std::ofstream out(kDefaultEventLogCsv);
                    if (!out) {
                        throw std::runtime_error("cannot open log file");
                    }
                    out << "timestamp,level,message\n";
                    for (const auto& entry : eventLog) {
                    out << formatTimestamp(entry.timestamp) << ',';
                    auto level_sv = spdlog::level::to_string_view(entry.level);
                    out.write(level_sv.data(), static_cast<std::streamsize>(level_sv.size()));
                    out << ',' << '"' << entry.message << '"' << '\n';
                    }
                    eventLog.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::info,
                                                        fmt::format("Event log exported to {}", kDefaultEventLogCsv.string())});
                } catch (const std::exception& ex) {
                    eventLog.emplace_back(EventLogEntry{std::chrono::system_clock::now(), spdlog::level::err,
                                                        fmt::format("Export failed: {}", ex.what())});
                }
                trimLog(eventLog);
            }
            ImGui::Separator();
            if (ImGui::BeginTable("logtable", 1, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Message");
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(eventLog.size()));
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        auto it = eventLog[eventLog.size() - 1 - i];
                        ImGui::TableNextRow();
                        ImVec4 color = (it.level >= spdlog::level::warn) ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(color, "[%s] %s", formatTimestamp(it.timestamp).c_str(), it.message.c_str());
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();

        if (ImGui::Begin("Status")) {
            ImGui::Text("Alias store: %s", aliasStore.path().string().c_str());
            ImGui::Text("OSC: %s:%d (broadcast=%s)", oscConfig.host.c_str(), oscConfig.port, oscConfig.broadcast ? "true" : "false");
            ImGui::Text("Selected: %zu", selectedDevices.size());
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

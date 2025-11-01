#include "acoustics/osc/OscPacket.h"

#include "CLI11.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::atomic_bool g_shouldStop{false};

void handleSignal(int) {
    g_shouldStop.store(true);
}

struct MonitorOptions {
    std::string listenHost{"0.0.0.0"};
    std::uint16_t port{9100};
    std::optional<std::filesystem::path> csv;
    std::uint64_t maxPackets{0};
    bool quiet{false};
};

struct DeviceStats {
    std::uint64_t count{0};
    double meanMs{0.0};
    double m2{0.0};
};

std::chrono::system_clock::time_point secondsToTimePoint(double seconds) {
    auto secs = static_cast<std::int64_t>(seconds);
    double fractional = seconds - static_cast<double>(secs);
    auto tp = std::chrono::system_clock::time_point{std::chrono::seconds{secs}} +
              std::chrono::duration_cast<std::chrono::system_clock::duration>(
                  std::chrono::duration<double>(fractional));
    return tp;
}

double toEpochSeconds(std::chrono::system_clock::time_point tp) {
    auto duration = tp.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

double argumentToSeconds(const acoustics::osc::Argument& arg) {
    if (const auto* f = std::get_if<float>(&arg)) {
        return static_cast<double>(*f);
    }
    if (const auto* i = std::get_if<std::int32_t>(&arg)) {
        return static_cast<double>(*i);
    }
    throw std::runtime_error("Unsupported timestamp argument type");
}

void updateStats(DeviceStats& stats, double sampleMs) {
    ++stats.count;
    double delta = sampleMs - stats.meanMs;
    stats.meanMs += delta / static_cast<double>(stats.count);
    double delta2 = sampleMs - stats.meanMs;
    stats.m2 += delta * delta2;
}

int createSocket(const MonitorOptions& options) {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create UDP socket: " + std::string(std::strerror(errno)));
    }
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.listenHost.c_str(), &addr.sin_addr) != 1) {
        ::close(sock);
        throw std::runtime_error("Invalid listen address: " + options.listenHost);
    }
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        throw std::runtime_error("Failed to bind UDP socket: " + std::string(std::strerror(errno)));
    }
    return sock;
}

std::ofstream openCsv(const std::filesystem::path& path) {
    const bool exists = std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open CSV file: " + path.string());
    }
    if (!exists) {
        out << "arrival_iso,device_id,sequence,latency_ms,sent_iso\n";
    }
    return out;
}

struct HeartbeatData {
    std::string deviceId;
    std::int32_t sequence;
    double sentSeconds;
};

HeartbeatData parseHeartbeat(const acoustics::osc::Message& message) {
    if (message.address != "/heartbeat" || message.arguments.size() < 3) {
        throw std::runtime_error("Not a heartbeat message");
    }
    HeartbeatData data;
    if (const auto* id = std::get_if<std::string>(&message.arguments[0])) {
        data.deviceId = *id;
    } else {
        throw std::runtime_error("Heartbeat device id must be a string");
    }
    if (const auto* seq = std::get_if<std::int32_t>(&message.arguments[1])) {
        data.sequence = *seq;
    } else {
        throw std::runtime_error("Heartbeat sequence must be int32");
    }
    data.sentSeconds = argumentToSeconds(message.arguments[2]);
    return data;
}

void emitSample(std::ostream& out,
                const HeartbeatData& data,
                double latencyMs,
                std::chrono::system_clock::time_point arrival) {
    auto arrivalTimeT = std::chrono::system_clock::to_time_t(arrival);
    auto arrivalLocal = *std::localtime(&arrivalTimeT);
    auto sentTimeT = std::chrono::system_clock::to_time_t(secondsToTimePoint(data.sentSeconds));
    auto sentLocal = *std::localtime(&sentTimeT);

    out << std::put_time(&arrivalLocal, "%Y-%m-%d %H:%M:%S")
        << "," << data.deviceId
        << "," << data.sequence
        << "," << std::fixed << std::setprecision(3) << latencyMs
        << "," << std::put_time(&sentLocal, "%Y-%m-%d %H:%M:%S")
        << '\n';
}

void processMessage(const acoustics::osc::Message& message,
                    const MonitorOptions& options,
                    std::unordered_map<std::string, DeviceStats>& stats,
                    std::ofstream* csvStream) {
    HeartbeatData data;
    try {
        data = parseHeartbeat(message);
    } catch (const std::exception&) {
        return;
    }

    auto arrival = std::chrono::system_clock::now();
    double arrivalSeconds = toEpochSeconds(arrival);
    double latencyMs = (arrivalSeconds - data.sentSeconds) * 1000.0;

    updateStats(stats[data.deviceId], latencyMs);

    if (!options.quiet) {
        std::cout << "[" << data.deviceId << "] seq=" << data.sequence
                  << " latency=" << std::fixed << std::setprecision(3) << latencyMs << " ms" << std::endl;
    }

    if (csvStream) {
        emitSample(*csvStream, data, latencyMs, arrival);
        csvStream->flush();
    }
}

void processPacket(const acoustics::osc::Packet& packet,
                   const MonitorOptions& options,
                   std::unordered_map<std::string, DeviceStats>& stats,
                   std::ofstream* csvStream) {
    if (const auto* message = std::get_if<acoustics::osc::Message>(&packet)) {
        processMessage(*message, options, stats, csvStream);
    } else if (const auto* bundle = std::get_if<acoustics::osc::Bundle>(&packet)) {
        for (const auto& msg : bundle->elements) {
            processMessage(msg, options, stats, csvStream);
        }
    }
}

void printSummary(const std::unordered_map<std::string, DeviceStats>& stats) {
    if (stats.empty()) {
        std::cout << "No heartbeat samples captured." << std::endl;
        return;
    }

    std::cout << "\nLatency summary (ms):\n";
    std::cout << std::left << std::setw(20) << "Device"
              << std::right << std::setw(10) << "Count"
              << std::setw(15) << "Mean"
              << std::setw(15) << "StdDev" << '\n';

    for (const auto& [device, stat] : stats) {
        double stddev = 0.0;
        if (stat.count > 1) {
            stddev = std::sqrt(stat.m2 / static_cast<double>(stat.count - 1));
        }
        std::cout << std::left << std::setw(20) << device
                  << std::right << std::setw(10) << stat.count
                  << std::setw(15) << std::fixed << std::setprecision(3) << stat.meanMs
                  << std::setw(15) << std::fixed << std::setprecision(3) << stddev
                  << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"Agent A Heartbeat Monitor"};
    MonitorOptions options;

    app.add_option("--host", options.listenHost, "Listen address (IPv4)");
    app.add_option("--port", options.port, "Listen port");
    app.add_option("--csv", options.csv, "Append results to CSV file");
    app.add_option("--count", options.maxPackets, "Stop after N packets (0 = unlimited)");
    app.add_flag("--quiet", options.quiet, "Suppress console output");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    std::signal(SIGINT, handleSignal);

    std::ofstream csvStream;
    if (options.csv.has_value()) {
        csvStream = openCsv(*options.csv);
    }

    try {
        int sock = createSocket(options);
        std::vector<std::uint8_t> buffer(4096);
        std::unordered_map<std::string, DeviceStats> stats;

        std::uint64_t processed = 0;
        while (!g_shouldStop.load()) {
            sockaddr_in src{};
            socklen_t srclen = sizeof(src);
            auto received = ::recvfrom(sock,
                                       reinterpret_cast<char*>(buffer.data()),
                                       buffer.size(),
                                       0,
                                       reinterpret_cast<sockaddr*>(&src),
                                       &srclen);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ::close(sock);
                throw std::runtime_error("recvfrom failed: " + std::string(std::strerror(errno)));
            }

            try {
                std::vector<std::uint8_t> payload(buffer.begin(), buffer.begin() + static_cast<std::size_t>(received));
                auto packet = acoustics::osc::decodePacket(payload);
                processPacket(packet, options, stats, options.csv ? &csvStream : nullptr);
            } catch (const std::exception& ex) {
                if (!options.quiet) {
                    std::cerr << "Discarded packet: " << ex.what() << '\n';
                }
            }
            ++processed;
            if (options.maxPackets > 0 && processed >= options.maxPackets) {
                break;
            }
        }

        ::close(sock);
        if (!options.quiet) {
            printSummary(stats);
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}

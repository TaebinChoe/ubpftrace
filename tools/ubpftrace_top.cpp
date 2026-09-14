#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

namespace {

volatile std::sig_atomic_t g_running = 1;

void sig_handler(int) {
    g_running = 0;
}

uint64_t get_now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

struct NodeSnapshot {
    uint32_t job_id{0};
    uint32_t node_id{0};
    std::string nodename;
    uint64_t timestamp_ns{0};
    uint32_t epoch{0};
    nlohmann::json maps;
    bool is_straggler{false};
};

struct AggregatedMap {
    std::string name;
    uint32_t type{0};
    uint64_t max_val{0};
    uint64_t min_val{UINT64_MAX};
    uint64_t sum_val{0};
    uint64_t count{0};
    std::map<uint64_t, uint64_t> hist_buckets;
    std::map<std::string, uint64_t> key_values;
};

void print_ascii_bar(uint64_t val, uint64_t max_val, int width = 30) {
    if (max_val == 0) max_val = 1;
    int bar_len = static_cast<int>((val * width) / max_val);
    if (bar_len > width) bar_len = width;
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < bar_len) std::cout << "=";
        else std::cout << " ";
    }
    std::cout << "] " << val;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    std::signal(SIGINT, sig_handler);
    std::signal(SIGTERM, sig_handler);

    std::string snap_dir = "";
    uint32_t target_job_id = 0;
    double interval_sec = 1.0;
    bool json_mode = false;
    bool run_once = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-d" || arg == "--dir") && i + 1 < argc) {
            snap_dir = argv[++i];
        } else if ((arg == "-j" || arg == "--job-id") && i + 1 < argc) {
            target_job_id = std::stoul(argv[++i]);
        } else if ((arg == "-i" || arg == "--interval") && i + 1 < argc) {
            interval_sec = std::stod(argv[++i]);
        } else if (arg == "--json") {
            json_mode = true;
        } else if (arg == "--once") {
            run_once = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: ubpftrace-top [options]\n\n"
                      << "Options:\n"
                      << "  -j, --job-id <ID>     Slurm Job ID to monitor\n"
                      << "  -d, --dir <PATH>      Directory containing live JSON snapshots\n"
                      << "  -i, --interval <SEC>  Refresh cadence in seconds (default: 1.0s)\n"
                      << "  --json                Emit cluster aggregation JSON once per interval\n"
                      << "  --once                Execute single snapshot reduction and exit\n"
                      << "  -h, --help            Display this help message\n";
            return 0;
        }
    }

    if (snap_dir.empty()) {
        if (target_job_id > 0) {
            if (const char *scratch = std::getenv("SCRATCH")) {
                snap_dir = std::string(scratch) + "/.ubpftrace_live_" + std::to_string(target_job_id);
            } else {
                snap_dir = "./.ubpftrace_live_" + std::to_string(target_job_id);
            }
        } else {
            // Find most recent live directory in $SCRATCH or current directory
            std::string search_root = ".";
            if (const char *scratch = std::getenv("SCRATCH")) {
                search_root = scratch;
            }
            try {
                for (const auto &entry : std::filesystem::directory_iterator(search_root)) {
                    if (entry.is_directory() && entry.path().filename().string().find(".ubpftrace_live_") == 0) {
                        snap_dir = entry.path().string();
                        break;
                    }
                }
            } catch (...) {}
            if (snap_dir.empty()) {
                snap_dir = ".";
            }
        }
    }

    if (!json_mode) {
        std::cout << "ubpftrace-top: Monitoring snapshot directory: " << snap_dir << std::endl;
    }

    uint64_t cycle = 0;
    while (g_running) {
        cycle++;
        std::vector<NodeSnapshot> nodes;
        uint64_t now_ns = get_now_ns();
        uint64_t max_node_ts = 0;

        // Ingest node snapshots
        try {
            if (std::filesystem::exists(snap_dir)) {
                for (const auto &entry : std::filesystem::directory_iterator(snap_dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".json" &&
                        entry.path().filename().string().find("node_") == 0) {
                        std::ifstream ifs(entry.path());
                        if (!ifs.is_open()) continue;

                        try {
                            nlohmann::json j;
                            ifs >> j;
                            NodeSnapshot snap;
                            snap.job_id = j.value("job_id", 0);
                            snap.node_id = j.value("node_id", 0);
                            snap.nodename = j.value("nodename", "unknown");
                            snap.timestamp_ns = j.value("timestamp_ns", 0ULL);
                            snap.epoch = j.value("epoch", 0);
                            snap.maps = j.value("maps", nlohmann::json::object());

                            if (snap.timestamp_ns > max_node_ts) {
                                max_node_ts = snap.timestamp_ns;
                            }
                            nodes.push_back(snap);
                        } catch (...) {
                            // Skip partially written or tearing files (handled by atomic rename, but safety check)
                        }
                    }
                }
            }
        } catch (...) {}

        // Check node stragglers
        uint64_t straggler_threshold_ns = static_cast<uint64_t>(interval_sec * 2.5 * 1e9);
        size_t straggler_count = 0;
        for (auto &node : nodes) {
            if (max_node_ts > 0 && max_node_ts - node.timestamp_ns > straggler_threshold_ns) {
                node.is_straggler = true;
                straggler_count++;
            }
        }

        // Multi-tier map aggregation
        std::map<std::string, AggregatedMap> agg_maps;

        for (const auto &node : nodes) {
            for (auto it = node.maps.begin(); it != node.maps.end(); ++it) {
                std::string map_name = it.key();
                const auto &map_data = it.value();

                AggregatedMap &agg = agg_maps[map_name];
                agg.name = map_name;
                agg.type = map_data.value("type", 0);

                if (map_data.contains("entries") && map_data["entries"].is_object()) {
                    for (auto e_it = map_data["entries"].begin(); e_it != map_data["entries"].end(); ++e_it) {
                        std::string k = e_it.key();
                        uint64_t v = 0;
                        if (e_it.value().is_number()) {
                            v = e_it.value().get<uint64_t>();
                        }

                        agg.count++;
                        agg.sum_val += v;
                        if (v > agg.max_val) agg.max_val = v;
                        if (v < agg.min_val) agg.min_val = v;

                        try {
                            uint64_t bucket_k = std::stoull(k);
                            agg.hist_buckets[bucket_k] += v;
                        } catch (...) {
                            agg.key_values[k] += v;
                        }
                    }
                }
            }
        }

        // Output Rendering
        if (json_mode) {
            nlohmann::json out_j;
            out_j["timestamp_ns"] = now_ns;
            out_j["active_nodes"] = nodes.size();
            out_j["straggler_nodes"] = straggler_count;

            nlohmann::json maps_out = nlohmann::json::object();
            for (const auto &[name, agg] : agg_maps) {
                nlohmann::json m;
                m["type"] = agg.type;
                m["count"] = agg.count;
                m["global_max"] = agg.max_val;
                m["global_min"] = (agg.min_val == UINT64_MAX) ? 0 : agg.min_val;
                m["global_sum"] = agg.sum_val;
                if (agg.count > 0) {
                    m["global_avg"] = static_cast<double>(agg.sum_val) / agg.count;
                }
                maps_out[name] = m;
            }
            out_j["maps"] = maps_out;
            std::cout << out_j.dump() << std::endl;
        } else {
            // ANSI Dashboard
            std::cout << "\033[2J\033[H"; // Clear terminal and home cursor
            std::cout << "================================================================================" << std::endl;
            std::cout << " ubpftrace-top :: Real-Time Cluster Aggregation Dashboard (Cycle #" << cycle << ")" << std::endl;
            std::cout << "================================================================================" << std::endl;
            std::cout << " Snapshot Dir : " << snap_dir << std::endl;
            std::cout << " Active Nodes : " << nodes.size() << " | Stragglers: "
                      << (straggler_count > 0 ? "\033[1;31m" : "\033[1;32m")
                      << straggler_count << "\033[0m"
                      << " | Interval: " << interval_sec << "s" << std::endl;
            std::cout << "--------------------------------------------------------------------------------" << std::endl;

            if (nodes.empty()) {
                std::cout << "\n  [Waiting for node snapshots in " << snap_dir << "...]\n" << std::endl;
            } else {
                std::cout << "\n[CLUSTER-WIDE METRIC AGGREGATIONS]" << std::endl;
                std::cout << std::left << std::setw(28) << "Map Name"
                          << std::right << std::setw(14) << "Global Max"
                          << std::setw(14) << "Global Min"
                          << std::setw(16) << "Global Sum"
                          << std::setw(8)  << "Entries" << std::endl;
                std::cout << std::string(80, '-') << std::endl;

                for (const auto &[name, agg] : agg_maps) {
                    uint64_t min_v = (agg.min_val == UINT64_MAX) ? 0 : agg.min_val;
                    std::cout << std::left << std::setw(28) << name
                              << std::right << std::setw(14) << agg.max_val
                              << std::setw(14) << min_v
                              << std::setw(16) << agg.sum_val
                              << std::setw(8)  << agg.count << std::endl;
                }

                // Render histograms if present
                for (const auto &[name, agg] : agg_maps) {
                    if (!agg.hist_buckets.empty() && agg.hist_buckets.size() > 1) {
                        std::cout << "\n[HISTOGRAM: " << name << "]" << std::endl;
                        uint64_t max_bin_val = 0;
                        for (const auto &[b, c] : agg.hist_buckets) {
                            if (c > max_bin_val) max_bin_val = c;
                        }
                        for (const auto &[b, c] : agg.hist_buckets) {
                            std::cout << "  " << std::setw(10) << b << " : ";
                            print_ascii_bar(c, max_bin_val, 25);
                            std::cout << std::endl;
                        }
                    }
                }

                // Render node status breakdown
                std::cout << "\n[NODE TOPOLOGY & SYNC STATUS]" << std::endl;
                std::cout << std::left << std::setw(10) << "Node ID"
                          << std::setw(20) << "Hostname"
                          << std::setw(10) << "Epoch"
                          << std::setw(20) << "Latency Lag (ms)"
                          << std::setw(15) << "Status" << std::endl;
                std::cout << std::string(75, '-') << std::endl;

                for (const auto &node : nodes) {
                    double lag_ms = 0.0;
                    if (max_node_ts >= node.timestamp_ns) {
                        lag_ms = (max_node_ts - node.timestamp_ns) / 1e6;
                    }
                    std::cout << std::left << std::setw(10) << node.node_id
                              << std::setw(20) << node.nodename
                              << std::setw(10) << node.epoch
                              << std::setw(20) << std::fixed << std::setprecision(2) << lag_ms
                              << (node.is_straggler ? "\033[1;31m[STRAGGLER]\033[0m" : "\033[1;32m[HEALTHY]\033[0m")
                              << std::endl;
                }
            }
            std::cout << "\n[Press Ctrl+C to stop monitoring]" << std::endl;
        }

        if (run_once) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(interval_sec * 1000)));
    }

    return 0;
}

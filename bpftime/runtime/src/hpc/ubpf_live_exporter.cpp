#include "hpc/ubpf_live_exporter.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <mutex>

namespace bpftime {
namespace hpc {

namespace {

uint64_t get_now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

std::mutex g_provider_mutex;
map_snapshot_callback_t g_map_snapshot_provider = nullptr;

} // anonymous namespace

void ubpf_live_exporter::set_map_snapshot_provider(map_snapshot_callback_t provider) {
    std::lock_guard<std::mutex> lock(g_provider_mutex);
    g_map_snapshot_provider = provider;
}

ubpf_live_exporter::ubpf_live_exporter(uint32_t job_id, uint32_t node_id, const std::string &nodename,
                                       const std::string &output_dir, uint64_t interval_ns,
                                       ubpf_node_shm_header *shm)
    : job_id_(job_id), node_id_(node_id), nodename_(nodename), output_dir_(output_dir), interval_ns_(interval_ns), shm_(shm) {
    try {
        std::filesystem::create_directories(output_dir_);
    } catch (...) {
        output_dir_ = ".";
    }

    final_filepath_ = output_dir_ + "/node_" + std::to_string(node_id_) + ".json";
    temp_filepath_ = output_dir_ + "/.node_" + std::to_string(node_id_) + ".json.tmp." + std::to_string(getpid());
}

ubpf_live_exporter::~ubpf_live_exporter() {
    try {
        std::filesystem::remove(temp_filepath_);
    } catch (...) {}
}

bool ubpf_live_exporter::export_snapshot() {
    uint32_t cur_epoch = epoch_.fetch_add(1, std::memory_order_relaxed);
    uint64_t now_ns = get_now_ns();

    nlohmann::json j;
    j["version"] = 1;
    j["job_id"] = job_id_;
    j["node_id"] = node_id_;
    j["nodename"] = nodename_;
    j["timestamp_ns"] = now_ns;
    j["epoch"] = cur_epoch;

    // Per-rank stats from SHM
    if (shm_) {
        nlohmann::json rank_stats_json = nlohmann::json::object();
        for (size_t r = 0; r < shm_->num_local_ranks && r < MAX_LOCAL_RANKS; ++r) {
            nlohmann::json r_obj;
            r_obj["recorded"] = shm_->rank_stats[r].recorded_events.load(std::memory_order_relaxed);
            r_obj["dropped"] = shm_->rank_stats[r].dropped_events.load(std::memory_order_relaxed);
            rank_stats_json[std::to_string(r)] = r_obj;
        }
        j["rank_stats"] = rank_stats_json;
    }

    // Maps from provider
    nlohmann::json maps_json = nlohmann::json::object();
    {
        std::lock_guard<std::mutex> lock(g_provider_mutex);
        if (g_map_snapshot_provider) {
            try {
                maps_json = g_map_snapshot_provider();
            } catch (...) {}
        }
    }
    j["maps"] = maps_json;

    // 1. Write to temporary hidden file
    FILE *fp = fopen(temp_filepath_.c_str(), "w");
    if (!fp) {
        return false;
    }

    std::string json_str = j.dump(2);
    size_t written = fwrite(json_str.data(), 1, json_str.size(), fp);
    fflush(fp);
    int fd = fileno(fp);
    if (fd >= 0) {
        fsync(fd);
    }
    fclose(fp);

    if (written != json_str.size()) {
        try { std::filesystem::remove(temp_filepath_); } catch (...) {}
        return false;
    }

    // 2. Atomic rename to final destination
    if (::rename(temp_filepath_.c_str(), final_filepath_.c_str()) != 0) {
        try { std::filesystem::remove(temp_filepath_); } catch (...) {}
        return false;
    }

    return true;
}

} // namespace hpc
} // namespace bpftime

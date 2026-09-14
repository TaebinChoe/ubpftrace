#ifndef _UBPF_LIVE_EXPORTER_HPP
#define _UBPF_LIVE_EXPORTER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <json.hpp>
#include "hpc/ubpf_shm_buffer.hpp"

namespace bpftime {
namespace hpc {

using map_snapshot_callback_t = std::function<nlohmann::json()>;

class ubpf_live_exporter {
public:
    ubpf_live_exporter(uint32_t job_id, uint32_t node_id, const std::string &nodename,
                       const std::string &output_dir, uint64_t interval_ns,
                       ubpf_node_shm_header *shm = nullptr);
    ~ubpf_live_exporter();

    bool export_snapshot();

    uint64_t get_interval_ns() const { return interval_ns_; }
    void set_interval_ns(uint64_t ns) { interval_ns_ = ns; }
    uint32_t get_epoch() const { return epoch_.load(std::memory_order_relaxed); }
    const std::string &get_output_filepath() const { return final_filepath_; }

    static void set_map_snapshot_provider(map_snapshot_callback_t provider);

private:
    uint32_t job_id_{0};
    uint32_t node_id_{0};
    std::string nodename_;
    std::string output_dir_;
    std::string final_filepath_;
    std::string temp_filepath_;
    uint64_t interval_ns_{1000000000ULL};
    ubpf_node_shm_header *shm_{nullptr};
    std::atomic<uint32_t> epoch_{0};
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_LIVE_EXPORTER_HPP

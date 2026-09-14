#ifndef _UBPF_AGENT_MANAGER_HPP
#define _UBPF_AGENT_MANAGER_HPP

#include "hpc/ubpf_shm_buffer.hpp"
#include "hpc/ubpf_container_writer.hpp"
#include "hpc/ubpf_async_io_worker.hpp"
#include "hpc/ubpf_live_exporter.hpp"
#include "hpc/ubpf_micro_streamer.hpp"
#include "hpc/ubpf_topology.hpp"

#include <memory>
#include <atomic>
#include <mutex>
#include <string>

namespace bpftime {
namespace hpc {

class ubpf_agent_manager {
public:
    static ubpf_agent_manager &instance();

    bool init();
    void shutdown();

    void log_event(uint16_t event_type, const void *payload, uint32_t payload_len);

    ubpf_node_shm_header *get_shm() const { return shm_; }
    bool is_initialized() const { return initialized_.load(std::memory_order_acquire); }

    uint32_t get_job_id() const { return job_id_; }
    uint32_t get_node_id() const { return node_id_; }
    uint32_t get_local_rank() const { return local_rank_; }
    uint32_t get_rank() const { return rank_; }
    const std::string &get_hostname() const { return hostname_; }
    const std::string &get_output_filepath() const { return output_filepath_; }

    ubpf_live_exporter *get_live_exporter() const { return live_exporter_.get(); }
    ubpf_micro_streamer *get_micro_streamer() const { return micro_streamer_.get(); }

private:
    ubpf_agent_manager() = default;
    ~ubpf_agent_manager();

    ubpf_agent_manager(const ubpf_agent_manager &) = delete;
    ubpf_agent_manager &operator=(const ubpf_agent_manager &) = delete;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_called_{false};
    std::mutex init_mutex_;

    ubpf_node_shm_header *shm_{nullptr};
    std::unique_ptr<ubpf_container_writer> writer_;
    std::unique_ptr<ubpf_live_exporter> live_exporter_;
    std::unique_ptr<ubpf_micro_streamer> micro_streamer_;
    std::unique_ptr<ubpf_async_io_worker> worker_;

    uint32_t job_id_{0};
    uint32_t node_id_{0};
    uint32_t local_rank_{0};
    uint32_t rank_{0};
    std::string hostname_;
    bool is_creator_{false};
    std::string output_filepath_;
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_AGENT_MANAGER_HPP

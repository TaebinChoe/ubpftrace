#ifndef _UBPF_ASYNC_IO_WORKER_HPP
#define _UBPF_ASYNC_IO_WORKER_HPP

#include "hpc/ubpf_shm_buffer.hpp"
#include "hpc/ubpf_container_writer.hpp"
#include "hpc/ubpf_live_exporter.hpp"
#include "hpc/ubpf_micro_streamer.hpp"

#include <thread>
#include <atomic>
#include <memory>

namespace bpftime {
namespace hpc {

class ubpf_async_io_worker {
public:
    ubpf_async_io_worker();
    ~ubpf_async_io_worker();

    bool start(ubpf_node_shm_header *shm,
               ubpf_container_writer *writer,
               uint64_t high_watermark_bytes = BUFFER_CAPACITY * 3 / 4,
               uint64_t soft_timer_ns = 2000000000ULL);

    bool start(ubpf_node_shm_header *shm,
               ubpf_container_writer *writer,
               ubpf_live_exporter *exporter,
               ubpf_micro_streamer *streamer,
               uint64_t high_watermark_bytes = BUFFER_CAPACITY * 3 / 4,
               uint64_t soft_timer_ns = 2000000000ULL);

    void stop();
    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    void trigger_force_flush();

private:
    void worker_loop();
    void setup_affinity_and_priority();
    void swap_and_flush(bool force);

    ubpf_node_shm_header *shm_{nullptr};
    ubpf_container_writer *writer_{nullptr};
    ubpf_live_exporter *exporter_{nullptr};
    ubpf_micro_streamer *streamer_{nullptr};

    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> force_flush_requested_{false};

    uint64_t high_watermark_bytes_{BUFFER_CAPACITY * 3 / 4};
    uint64_t soft_timer_ns_{2000000000ULL};
    uint64_t last_flush_ns_{0};
    uint64_t last_live_export_ns_{0};
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_ASYNC_IO_WORKER_HPP

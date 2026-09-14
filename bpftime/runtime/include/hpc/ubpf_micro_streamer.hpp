#ifndef _UBPF_MICRO_STREAMER_HPP
#define _UBPF_MICRO_STREAMER_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include "hpc/ubpf_shm_buffer.hpp"

namespace bpftime {
namespace hpc {

class ubpf_micro_streamer {
public:
    ubpf_micro_streamer(uint64_t flush_timeout_ns = 20000000ULL, // 20ms
                        size_t buffer_threshold_bytes = 8192);    // 8 KB
    ~ubpf_micro_streamer();

    // Process a raw chunk of event records from SHM buffer
    void process_chunk(const uint8_t *data, size_t len);

    // Periodic check to flush if soft timer exceeded
    void check_timer_and_flush();

    // Force flush all pending buffered text to stdout
    void flush();

    void set_flush_timeout_ns(uint64_t ns) { flush_timeout_ns_ = ns; }
    void set_buffer_threshold_bytes(size_t bytes) { buffer_threshold_bytes_ = bytes; }

private:
    uint64_t flush_timeout_ns_{20000000ULL};
    size_t buffer_threshold_bytes_{8192};
    uint64_t last_flush_ns_{0};

    std::vector<char> batch_buffer_;
    std::mutex stream_mutex_;
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_MICRO_STREAMER_HPP

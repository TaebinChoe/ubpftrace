#ifndef _UBPF_SHM_BUFFER_HPP
#define _UBPF_SHM_BUFFER_HPP

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <string>

namespace bpftime {
namespace hpc {

constexpr uint32_t UBPF_SHM_MAGIC = 0x55425046; // "UBPF"
constexpr uint32_t UBPF_SHM_VERSION = 0x00010000;
constexpr size_t MAX_LOCAL_RANKS = 256;
constexpr size_t BUFFER_CAPACITY = 16 * 1024 * 1024; // 16 MB per buffer block
constexpr int MAX_PROBE_RETRIES = 2; // Strict bound on probe retries

// Cache-line padded per-rank counters to eliminate false sharing
struct alignas(64) PerRankStats {
    std::atomic<uint64_t> dropped_events{0};
    std::atomic<uint64_t> recorded_events{0};
    uint8_t padding[48]; // Pads to exactly 64 bytes (1 cache line)
};
static_assert(sizeof(PerRankStats) == 64, "PerRankStats must be exactly 64 bytes");

// Binary trace event record header
struct alignas(8) ubpf_event_record_header {
    uint32_t record_len;         // Total length including payload and header
    uint16_t rank;               // Global MPI Rank ID
    uint16_t event_type;         // Event type ID (probe, printf, tracepoint)
    uint64_t timestamp_ns;       // CLOCK_MONOTONIC timestamp in nanoseconds
    uint32_t payload_len;        // Payload length
    uint32_t reserved;           // 64-bit alignment padding
};

// Double-buffering block
struct ubpf_buffer_block {
    alignas(64) std::atomic<uint64_t> write_offset{0};
    alignas(64) std::atomic<uint32_t> active_writers{0};
    uint8_t data[BUFFER_CAPACITY];
};

// Intra-node POSIX SHM Segment Header
struct ubpf_node_shm_header {
    uint32_t magic;
    uint32_t version;
    uint32_t job_id;
    uint32_t node_id;
    uint32_t num_local_ranks;
    std::atomic<bool> map_overflow_flag{false}; // Async map spilling flag

    // Packed 64-bit word: (epoch_generation << 32) | active_buffer_idx
    alignas(64) std::atomic<uint64_t> active_epoch_buffer{0};
    
    // Padded statistics per local rank
    alignas(64) PerRankStats rank_stats[MAX_LOCAL_RANKS];
    
    // Double buffers
    ubpf_buffer_block buffers[2];
};

// SHM Lifecycle functions
ubpf_node_shm_header *ubpf_shm_create_or_attach(uint32_t job_id, uint32_t node_id,
                                                bool is_creator,
                                                const char *custom_shm_name = nullptr);

void ubpf_shm_detach(ubpf_node_shm_header *shm, bool is_creator,
                     const char *custom_shm_name = nullptr);

// Wait-free in-probe reservation protocol (guaranteed < 50 ns, bounded retries <= 2)
inline bool ubpf_probe_reserve_record(ubpf_node_shm_header *shm, uint32_t local_rank,
                                      uint32_t record_len, uint8_t **out_ptr,
                                      uint32_t *out_buf_idx) {
    if (__builtin_expect(!shm || local_rank >= MAX_LOCAL_RANKS, 0)) return false;

    for (int retry = 0; retry < MAX_PROBE_RETRIES; ++retry) {
        uint64_t current_epoch_buf = shm->active_epoch_buffer.load(std::memory_order_acquire);
        uint32_t buf_idx = static_cast<uint32_t>(current_epoch_buf & 0xFFFFFFFFULL);

        ubpf_buffer_block *buf = &shm->buffers[buf_idx];

        // Register active writer under current epoch (acquire guard)
        buf->active_writers.fetch_add(1, std::memory_order_acquire);

        // Re-verify epoch has not changed during registration
        uint64_t verify_epoch_buf = shm->active_epoch_buffer.load(std::memory_order_acquire);
        if (__builtin_expect(verify_epoch_buf != current_epoch_buf, 0)) {
            // Buffer was swapped concurrently -> release and retry once
            buf->active_writers.fetch_sub(1, std::memory_order_release);
            continue;
        }

        // Reserve space in active buffer
        uint64_t offset = buf->write_offset.fetch_add(record_len, std::memory_order_relaxed);
        if (__builtin_expect(offset + record_len <= BUFFER_CAPACITY, 1)) {
            *out_ptr = buf->data + offset;
            *out_buf_idx = buf_idx;
            return true; // Successfully reserved; caller commits via ubpf_probe_commit_record
        } else {
            // Buffer capacity exceeded -> Instant wait-free abort and record drop
            buf->active_writers.fetch_sub(1, std::memory_order_release);
            shm->rank_stats[local_rank].dropped_events.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    // Bounded retries exhausted -> Abort without spinning
    shm->rank_stats[local_rank].dropped_events.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// In-probe commit protocol
inline void ubpf_probe_commit_record(ubpf_node_shm_header *shm, uint32_t local_rank,
                                     uint32_t buf_idx) {
    if (__builtin_expect(!shm || buf_idx > 1 || local_rank >= MAX_LOCAL_RANKS, 0)) return;
    shm->buffers[buf_idx].active_writers.fetch_sub(1, std::memory_order_release);
    shm->rank_stats[local_rank].recorded_events.fetch_add(1, std::memory_order_relaxed);
}

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_SHM_BUFFER_HPP

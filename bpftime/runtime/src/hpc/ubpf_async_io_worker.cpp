#include "hpc/ubpf_async_io_worker.hpp"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#include <chrono>
#include <iostream>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace bpftime {
namespace hpc {

namespace {

uint64_t get_now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

} // anonymous namespace

ubpf_async_io_worker::ubpf_async_io_worker() = default;

ubpf_async_io_worker::~ubpf_async_io_worker() {
    stop();
}

bool ubpf_async_io_worker::start(ubpf_node_shm_header *shm,
                                 ubpf_container_writer *writer,
                                 uint64_t high_watermark_bytes,
                                 uint64_t soft_timer_ns) {
    return start(shm, writer, nullptr, nullptr, high_watermark_bytes, soft_timer_ns);
}

bool ubpf_async_io_worker::start(ubpf_node_shm_header *shm,
                                 ubpf_container_writer *writer,
                                 ubpf_live_exporter *exporter,
                                 ubpf_micro_streamer *streamer,
                                 uint64_t high_watermark_bytes,
                                 uint64_t soft_timer_ns) {
    if (!shm || running_.load(std::memory_order_relaxed)) {
        return false;
    }

    shm_ = shm;
    writer_ = writer;
    exporter_ = exporter;
    streamer_ = streamer;
    high_watermark_bytes_ = high_watermark_bytes;
    soft_timer_ns_ = soft_timer_ns;
    last_flush_ns_ = get_now_ns();
    last_live_export_ns_ = get_now_ns();
    stop_requested_.store(false, std::memory_order_relaxed);
    force_flush_requested_.store(false, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    worker_thread_ = std::thread(&ubpf_async_io_worker::worker_loop, this);
    return true;
}

void ubpf_async_io_worker::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    stop_requested_.store(true, std::memory_order_release);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // Perform one final forced flush
    if (shm_) {
        swap_and_flush(true);
        if (writer_) {
            writer_->flush();
        }
        if (streamer_) {
            streamer_->flush();
        }
    }
}

void ubpf_async_io_worker::trigger_force_flush() {
    force_flush_requested_.store(true, std::memory_order_release);
}

void ubpf_async_io_worker::setup_affinity_and_priority() {
    // 1. Attempt to break out of inherited single-core CPU pinning
    cpu_set_t full_mask;
    CPU_ZERO(&full_mask);

    long num_procs = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_procs > 0) {
        for (long i = 0; i < num_procs; ++i) {
            CPU_SET(i, &full_mask);
        }
    }

    int affinity_ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &full_mask);
    if (affinity_ret != 0) {
        // Fallback: Proceed if setting affinity outside Slurm cgroup is restricted
    }

    // 2. Set worker scheduling priority to SCHED_IDLE or nice level +19
    struct sched_param param;
    param.sched_priority = 0;
    if (pthread_setschedparam(pthread_self(), SCHED_IDLE, &param) != 0) {
        // If SCHED_IDLE is unsupported or restricted, degrade nice level to +19
        setpriority(PRIO_PROCESS, 0, 19);
    }
}

void ubpf_async_io_worker::worker_loop() {
    setup_affinity_and_priority();

    while (!stop_requested_.load(std::memory_order_acquire)) {
        uint64_t current_val = shm_->active_epoch_buffer.load(std::memory_order_acquire);
        uint32_t active_idx = static_cast<uint32_t>(current_val & 0xFFFFFFFFULL);

        uint64_t current_occupancy = shm_->buffers[active_idx].write_offset.load(std::memory_order_relaxed);
        uint64_t now_ns = get_now_ns();
        uint64_t elapsed_ns = now_ns - last_flush_ns_;

        // Check periodic live map snapshot export (Scenario A)
        if (exporter_) {
            if (now_ns - last_live_export_ns_ >= exporter_->get_interval_ns()) {
                exporter_->export_snapshot();
                last_live_export_ns_ = now_ns;
            }
        }

        // Check periodic micro-streaming timer
        if (streamer_) {
            streamer_->check_timer_and_flush();
        }

        bool watermark_triggered = (current_occupancy >= high_watermark_bytes_);
        bool timer_triggered = (elapsed_ns >= soft_timer_ns_ && current_occupancy > 0);
        bool force_triggered = force_flush_requested_.exchange(false, std::memory_order_acq_rel);

        if (watermark_triggered || timer_triggered || force_triggered) {
            swap_and_flush(force_triggered);
        }

        // 1ms sleep cadence to maintain high-resolution sub-second responsiveness
        usleep(1000);
    }

    // Flush any remaining buffered records before thread exit
    swap_and_flush(true);
}

void ubpf_async_io_worker::swap_and_flush(bool force) {
    if (!shm_) return;

    uint64_t current_val = shm_->active_epoch_buffer.load(std::memory_order_acquire);
    uint32_t epoch = static_cast<uint32_t>(current_val >> 32);
    uint32_t active_idx = static_cast<uint32_t>(current_val & 0xFFFFFFFFULL);

    ubpf_buffer_block *buf = &shm_->buffers[active_idx];
    uint64_t current_offset = buf->write_offset.load(std::memory_order_relaxed);

    if (current_offset == 0 && !force) {
        return; // No records to flush
    }

    // 1. Advance epoch generation and flip active buffer index
    uint32_t next_epoch = epoch + 1;
    uint32_t next_idx = 1 - active_idx;
    uint64_t next_val = (static_cast<uint64_t>(next_epoch) << 32) | next_idx;
    shm_->active_epoch_buffer.store(next_val, std::memory_order_release);

    // 2. Hazard drain wait: Spin until all writers observing the previous epoch have committed
    int spin_count = 0;
    while (buf->active_writers.load(std::memory_order_acquire) > 0) {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
#endif
        if (++spin_count > 1000000) {
            usleep(100);
        }
    }

    // 3. Read total uncompressed bytes written
    uint64_t uncompressed_len = buf->write_offset.load(std::memory_order_acquire);
    if (uncompressed_len > BUFFER_CAPACITY) {
        uncompressed_len = BUFFER_CAPACITY;
    }

    if (uncompressed_len > 0) {
        // Dispatch to micro-buffered live streamer if enabled
        if (streamer_) {
            streamer_->process_chunk(buf->data, uncompressed_len);
        }

        // Aggregate per-rank stats across all local ranks
        uint64_t total_dropped = 0;
        uint64_t total_recorded = 0;
        for (size_t r = 0; r < MAX_LOCAL_RANKS; ++r) {
            total_dropped += shm_->rank_stats[r].dropped_events.load(std::memory_order_relaxed);
            total_recorded += shm_->rank_stats[r].recorded_events.load(std::memory_order_relaxed);
        }

        uint64_t now_ns = get_now_ns();

        // Write compressed chunk to Lustre container file if writer is open
        if (writer_) {
            writer_->write_chunk(CHUNK_TYPE_EVENT_STREAM,
                                 buf->data,
                                 static_cast<uint32_t>(uncompressed_len),
                                 static_cast<uint32_t>(total_recorded),
                                 static_cast<uint32_t>(total_dropped),
                                 last_flush_ns_,
                                 now_ns);
        }
        last_flush_ns_ = now_ns;
    }

    // 4. Safely recycle buffer
    buf->write_offset.store(0, std::memory_order_release);
}

} // namespace hpc
} // namespace bpftime

#include "hpc/ubpf_agent_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace bpftime {
namespace hpc {

namespace {

uint64_t get_now_ns_monotonic() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

void atexit_handler() {
    ubpf_agent_manager::instance().shutdown();
}

} // anonymous namespace

ubpf_agent_manager &ubpf_agent_manager::instance() {
    static ubpf_agent_manager mgr;
    return mgr;
}

ubpf_agent_manager::~ubpf_agent_manager() {
    shutdown();
}

bool ubpf_agent_manager::init() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    shutdown_called_.store(false, std::memory_order_relaxed);

    // 1. Discover Slurm / PMIx / OpenMPI cluster topology
    ubpf_node_topology topo = discover_topology();
    job_id_ = topo.job_id;
    node_id_ = topo.node_id;
    local_rank_ = topo.local_rank;
    rank_ = topo.rank;
    hostname_ = topo.nodename;
    is_creator_ = (local_rank_ == 0);

    // 2. Resolve output directory for Lustre .ubpf container files
    std::string out_dir = ".";
    if (const char *env_dir = std::getenv("UBPFTRACE_OUTPUT_DIR")) {
        out_dir = env_dir;
    } else if (const char *scratch = std::getenv("SCRATCH")) {
        out_dir = scratch;
    }

    // Ensure output directory exists
    try {
        std::filesystem::create_directories(out_dir);
    } catch (...) {
        out_dir = ".";
    }

    output_filepath_ = out_dir + "/ubpftrace_" + std::to_string(job_id_) +
                       "_node_" + std::to_string(node_id_) + ".ubpf";

    // 3. Attach or create node-local POSIX SHM segment
    shm_ = ubpf_shm_create_or_attach(job_id_, node_id_, is_creator_);
    if (!shm_) {
        return false;
    }

    // Check environment variables for Scenario A live snapshotting and live micro-streaming
    uint64_t live_interval_ms = 0;
    if (const char *env_live_ms = std::getenv("UBPFTRACE_LIVE_INTERVAL_MS")) {
        live_interval_ms = std::strtoull(env_live_ms, nullptr, 10);
    } else if (const char *env_live_sec = std::getenv("UBPFTRACE_LIVE_INTERVAL_SEC")) {
        live_interval_ms = std::strtoull(env_live_sec, nullptr, 10) * 1000ULL;
    }

    std::string live_dir = out_dir + "/.ubpftrace_live_" + std::to_string(job_id_);
    if (const char *env_live_dir = std::getenv("UBPFTRACE_LIVE_DIR")) {
        live_dir = env_live_dir;
    }

    bool stream_enabled = false;
    if (const char *env_stream = std::getenv("UBPFTRACE_STREAM")) {
        stream_enabled = (std::strcmp(env_stream, "1") == 0 || std::strcmp(env_stream, "true") == 0);
    }

    uint64_t stream_flush_ms = 20;
    if (const char *env_flush_ms = std::getenv("UBPFTRACE_STREAM_FLUSH_MS")) {
        stream_flush_ms = std::strtoull(env_flush_ms, nullptr, 10);
    }

    size_t stream_buffer_kb = 8;
    if (const char *env_buf_kb = std::getenv("UBPFTRACE_STREAM_BUFFER_KB")) {
        stream_buffer_kb = std::strtoul(env_buf_kb, nullptr, 10);
    }

    // 4. If node leader (local_rank == 0), instantiate exporter, streamer, container writer and background I/O worker
    if (is_creator_) {
        if (live_interval_ms > 0) {
            live_exporter_ = std::make_unique<ubpf_live_exporter>(
                job_id_, node_id_, hostname_, live_dir, live_interval_ms * 1000000ULL);
        }

        if (stream_enabled) {
            micro_streamer_ = std::make_unique<ubpf_micro_streamer>(
                stream_flush_ms * 1000000ULL, stream_buffer_kb * 1024);
        }

        writer_ = std::make_unique<ubpf_container_writer>();
        if (!writer_->open(output_filepath_, job_id_, node_id_, hostname_.c_str())) {
            writer_.reset();
        }

        worker_ = std::make_unique<ubpf_async_io_worker>();
        worker_->start(shm_, writer_.get(), live_exporter_.get(), micro_streamer_.get());
    }

    // 5. Register exit hooks for clean container finalization
    std::atexit(atexit_handler);

    initialized_.store(true, std::memory_order_release);
    return true;
}

void ubpf_agent_manager::shutdown() {
    if (shutdown_called_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    // 1. Stop asynchronous I/O worker and flush pending buffer blocks
    if (worker_) {
        worker_->stop();
        worker_.reset();
    }

    // 2. Close and finalize Lustre container writer (appends index and UBPFTRLR)
    if (writer_) {
        writer_->close();
        writer_.reset();
    }

    // 3. Reset live exporter and micro streamer
    if (live_exporter_) {
        live_exporter_.reset();
    }
    if (micro_streamer_) {
        micro_streamer_.reset();
    }

    // 4. Detach and unlink SHM
    if (shm_) {
        ubpf_shm_detach(shm_, is_creator_);
        shm_ = nullptr;
    }

    initialized_.store(false, std::memory_order_release);
}

void ubpf_agent_manager::log_event(uint16_t event_type, const void *payload, uint32_t payload_len) {
    if (__builtin_expect(!initialized_.load(std::memory_order_relaxed), 0)) {
        if (!init()) return;
    }

    if (__builtin_expect(!shm_, 0)) return;

    uint32_t record_len = static_cast<uint32_t>(sizeof(ubpf_event_record_header) + payload_len);
    uint8_t *record_ptr = nullptr;
    uint32_t buf_idx = 0;

    // Wait-free reservation into epoch-hazard lockless SHM
    if (ubpf_probe_reserve_record(shm_, local_rank_, record_len, &record_ptr, &buf_idx)) {
        auto *hdr = reinterpret_cast<ubpf_event_record_header *>(record_ptr);
        hdr->record_len = record_len;
        hdr->rank = static_cast<uint16_t>(rank_);
        hdr->event_type = event_type;
        hdr->timestamp_ns = get_now_ns_monotonic();
        hdr->payload_len = payload_len;
        hdr->reserved = 0;

        if (payload && payload_len > 0) {
            std::memcpy(record_ptr + sizeof(ubpf_event_record_header), payload, payload_len);
        }

        // Commit reservation
        ubpf_probe_commit_record(shm_, local_rank_, buf_idx);
    }
}

} // namespace hpc
} // namespace bpftime

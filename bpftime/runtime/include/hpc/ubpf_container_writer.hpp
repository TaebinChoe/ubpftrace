#ifndef _UBPF_CONTAINER_WRITER_HPP
#define _UBPF_CONTAINER_WRITER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>

namespace bpftime {
namespace hpc {

constexpr uint32_t UBPF_FILE_MAGIC = 0x55425046; // "UBPF"
constexpr uint32_t UBPF_FILE_VERSION = 0x00010000;
constexpr uint32_t UBPF_CHUNK_MAGIC = 0x43484E4B; // "CHNK"
constexpr uint64_t UBPF_TRAILER_MAGIC = 0x5542504654524C52ULL; // "UBPFTRLR"

constexpr uint16_t CHUNK_TYPE_EVENT_STREAM = 0x0001;
constexpr uint16_t CHUNK_TYPE_MAP_SNAPSHOT = 0x0002;

constexpr uint16_t CODEC_NONE = 0x00;
constexpr uint16_t CODEC_LZ4  = 0x01;
constexpr uint16_t CODEC_ZSTD = 0x02;

// 2MB Lustre OST stripe alignment buffer
constexpr size_t LUSTRE_STRIPE_BLOCK_SIZE = 2 * 1024 * 1024;

// 128-byte static file header
struct alignas(64) ubpf_file_header {
    uint32_t magic;               // 0x55425046 ("UBPF")
    uint32_t version;             // 0x00010000
    uint32_t job_id;
    uint32_t node_id;
    char hostname[64];
    uint16_t codec_id;            // 0x01 = LZ4
    uint16_t reserved16;
    uint32_t reserved32;          // Explicit alignment padding to 8-byte boundary
    uint64_t timestamp_base_ns;   // CLOCK_MONOTONIC start (ns)
    uint64_t wallclock_base_ns;   // CLOCK_REALTIME start (Epoch ns)
    uint8_t reserved[24];         // Exactly pads struct to 128 bytes
};
static_assert(sizeof(ubpf_file_header) == 128, "ubpf_file_header must be 128 bytes");

// 64-byte chunk header
struct alignas(64) ubpf_chunk_header {
    uint32_t chunk_magic;         // 0x43484E4B ("CHNK")
    uint16_t chunk_type;          // 0x01: EVENT_STREAM, 0x02: MAP_SNAPSHOT
    uint16_t codec_id;            // 0x01: LZ4
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    uint64_t chunk_start_ns;
    uint64_t chunk_end_ns;
    uint32_t record_count;
    uint32_t dropped_events_count;
    uint32_t crc32;
    uint32_t reserved[5];         // Exactly pads struct to 64 bytes
};
static_assert(sizeof(ubpf_chunk_header) == 64, "ubpf_chunk_header must be 64 bytes");

// Index table entry in footer
struct ubpf_chunk_index_entry {
    uint64_t file_offset;
    uint32_t compressed_size;
    uint16_t chunk_type;
    uint16_t codec_id;
    uint64_t start_ns;
    uint64_t end_ns;
    uint32_t record_count;
    uint32_t dropped_count;
};

// Trailer struct at file end
struct ubpf_file_trailer {
    uint32_t total_chunks;
    uint64_t total_records;
    uint64_t total_dropped;
    uint64_t index_table_offset;
    uint64_t trailer_magic;       // 0x5542504654524C52 ("UBPFTRLR")
};

class ubpf_container_writer {
public:
    ubpf_container_writer();
    ~ubpf_container_writer();

    bool open(const std::string &filepath, uint32_t job_id, uint32_t node_id,
              const char *hostname);

    bool write_chunk(uint16_t chunk_type, const void *uncompressed_data,
                     uint32_t uncompressed_len, uint32_t record_count,
                     uint32_t dropped_count, uint64_t start_ns, uint64_t end_ns);

    void flush();
    void close();
    bool is_open() const { return fd_ >= 0; }

    uint64_t get_total_records() const { return total_records_; }
    uint64_t get_total_dropped() const { return total_dropped_; }
    uint32_t get_total_chunks() const { return static_cast<uint32_t>(index_table_.size()); }

private:
    int fd_{-1};
    std::string filepath_;
    uint64_t current_file_offset_{0};
    uint64_t total_records_{0};
    uint64_t total_dropped_{0};
    std::vector<ubpf_chunk_index_entry> index_table_;
    std::mutex write_mutex_;

    // Staging buffer for Lustre OST 2MB alignment batching
    std::vector<uint8_t> staging_buffer_;
    void flush_staging_buffer_locked();
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_CONTAINER_WRITER_HPP

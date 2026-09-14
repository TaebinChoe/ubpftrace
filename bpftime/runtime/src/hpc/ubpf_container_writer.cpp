#include "hpc/ubpf_container_writer.hpp"

#include <lz4.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <cstring>
#include <chrono>
#include <iostream>

namespace bpftime {
namespace hpc {

namespace {

// Fast IEEE 802.3 CRC32 calculation
uint32_t compute_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

uint64_t get_current_ns_monotonic() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

uint64_t get_current_ns_wallclock() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

} // anonymous namespace

ubpf_container_writer::ubpf_container_writer() {
    staging_buffer_.reserve(LUSTRE_STRIPE_BLOCK_SIZE);
}

ubpf_container_writer::~ubpf_container_writer() {
    close();
}

bool ubpf_container_writer::open(const std::string &filepath, uint32_t job_id,
                                 uint32_t node_id, const char *hostname) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) return true;

    filepath_ = filepath;
    fd_ = ::open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd_ < 0) {
        return false;
    }

    ubpf_file_header fhdr;
    std::memset(&fhdr, 0, sizeof(fhdr));
    fhdr.magic = UBPF_FILE_MAGIC;
    fhdr.version = UBPF_FILE_VERSION;
    fhdr.job_id = job_id;
    fhdr.node_id = node_id;
    if (hostname) {
        std::strncpy(fhdr.hostname, hostname, sizeof(fhdr.hostname) - 1);
    }
    fhdr.codec_id = CODEC_LZ4;
    fhdr.timestamp_base_ns = get_current_ns_monotonic();
    fhdr.wallclock_base_ns = get_current_ns_wallclock();

    ssize_t written = ::write(fd_, &fhdr, sizeof(fhdr));
    if (written != static_cast<ssize_t>(sizeof(fhdr))) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    current_file_offset_ = sizeof(fhdr);
    total_records_ = 0;
    total_dropped_ = 0;
    index_table_.clear();
    staging_buffer_.clear();
    return true;
}

bool ubpf_container_writer::write_chunk(uint16_t chunk_type,
                                       const void *uncompressed_data,
                                       uint32_t uncompressed_len,
                                       uint32_t record_count,
                                       uint32_t dropped_count,
                                       uint64_t start_ns,
                                       uint64_t end_ns) {
    if (!uncompressed_data || uncompressed_len == 0 || fd_ < 0) return false;

    std::lock_guard<std::mutex> lock(write_mutex_);

    // 1. Compress with LZ4
    int max_compressed_size = LZ4_compressBound(static_cast<int>(uncompressed_len));
    std::vector<uint8_t> comp_buf(max_compressed_size);

    int compressed_len = LZ4_compress_default(
        static_cast<const char *>(uncompressed_data),
        reinterpret_cast<char *>(comp_buf.data()),
        static_cast<int>(uncompressed_len),
        max_compressed_size
    );

    if (compressed_len <= 0) {
        return false;
    }

    // 2. Prepare chunk header
    ubpf_chunk_header chdr;
    std::memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = UBPF_CHUNK_MAGIC;
    chdr.chunk_type = chunk_type;
    chdr.codec_id = CODEC_LZ4;
    chdr.uncompressed_size = uncompressed_len;
    chdr.compressed_size = static_cast<uint32_t>(compressed_len);
    chdr.chunk_start_ns = start_ns;
    chdr.chunk_end_ns = end_ns;
    chdr.record_count = record_count;
    chdr.dropped_events_count = dropped_count;
    chdr.crc32 = compute_crc32(comp_buf.data(), static_cast<size_t>(compressed_len));

    // Record index entry
    uint64_t chunk_file_pos = current_file_offset_ + staging_buffer_.size();
    ubpf_chunk_index_entry idx_entry;
    idx_entry.file_offset = chunk_file_pos;
    idx_entry.compressed_size = static_cast<uint32_t>(compressed_len);
    idx_entry.chunk_type = chunk_type;
    idx_entry.codec_id = CODEC_LZ4;
    idx_entry.start_ns = start_ns;
    idx_entry.end_ns = end_ns;
    idx_entry.record_count = record_count;
    idx_entry.dropped_count = dropped_count;
    index_table_.push_back(idx_entry);

    total_records_ += record_count;
    total_dropped_ += dropped_count;

    // 3. Stage chunk into Lustre OST aligned buffer
    size_t prev_size = staging_buffer_.size();
    staging_buffer_.resize(prev_size + sizeof(chdr) + compressed_len);
    std::memcpy(staging_buffer_.data() + prev_size, &chdr, sizeof(chdr));
    std::memcpy(staging_buffer_.data() + prev_size + sizeof(chdr), comp_buf.data(), compressed_len);

    // If staging buffer exceeds 2MB stripe block, flush to disk
    if (staging_buffer_.size() >= LUSTRE_STRIPE_BLOCK_SIZE) {
        flush_staging_buffer_locked();
    }

    return true;
}

void ubpf_container_writer::flush_staging_buffer_locked() {
    if (fd_ < 0 || staging_buffer_.empty()) return;

    size_t to_write = staging_buffer_.size();
    const uint8_t *ptr = staging_buffer_.data();

    while (to_write > 0) {
        ssize_t written = ::write(fd_, ptr, to_write);
        if (written <= 0) {
            break;
        }
        ptr += written;
        to_write -= written;
        current_file_offset_ += written;
    }

    staging_buffer_.clear();
}

void ubpf_container_writer::flush() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    flush_staging_buffer_locked();
}

void ubpf_container_writer::close() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ < 0) return;

    // Flush remaining staged chunks
    flush_staging_buffer_locked();

    // Write index table
    uint64_t index_table_offset = current_file_offset_;
    if (!index_table_.empty()) {
        size_t index_bytes = index_table_.size() * sizeof(ubpf_chunk_index_entry);
        ssize_t written = ::write(fd_, index_table_.data(), index_bytes);
        if (written > 0) {
            current_file_offset_ += written;
        }
    }

    // Write trailer
    ubpf_file_trailer trailer;
    std::memset(&trailer, 0, sizeof(trailer));
    trailer.total_chunks = static_cast<uint32_t>(index_table_.size());
    trailer.total_records = total_records_;
    trailer.total_dropped = total_dropped_;
    trailer.index_table_offset = index_table_offset;
    trailer.trailer_magic = UBPF_TRAILER_MAGIC;

    ::write(fd_, &trailer, sizeof(trailer));
    ::close(fd_);
    fd_ = -1;
}

} // namespace hpc
} // namespace bpftime

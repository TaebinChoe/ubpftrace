#include "hpc/ubpf_micro_streamer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace bpftime {
namespace hpc {

namespace {

uint64_t get_now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

} // anonymous namespace

ubpf_micro_streamer::ubpf_micro_streamer(uint64_t flush_timeout_ns, size_t buffer_threshold_bytes)
    : flush_timeout_ns_(flush_timeout_ns), buffer_threshold_bytes_(buffer_threshold_bytes) {
    last_flush_ns_ = get_now_ns();
    batch_buffer_.reserve(buffer_threshold_bytes_ * 2);
}

ubpf_micro_streamer::~ubpf_micro_streamer() {
    flush();
}

void ubpf_micro_streamer::process_chunk(const uint8_t *data, size_t len) {
    if (!data || len < sizeof(ubpf_event_record_header)) {
        return;
    }

    std::lock_guard<std::mutex> lock(stream_mutex_);

    // 1. Collect all record headers
    std::vector<const ubpf_event_record_header *> records;
    size_t offset = 0;
    while (offset + sizeof(ubpf_event_record_header) <= len) {
        const auto *hdr = reinterpret_cast<const ubpf_event_record_header *>(data + offset);
        if (hdr->record_len < sizeof(ubpf_event_record_header) || offset + hdr->record_len > len) {
            break; // Corrupted or truncated record boundary
        }
        records.push_back(hdr);
        offset += hdr->record_len;
    }

    if (records.empty()) {
        return;
    }

    // 2. Intra-node chronological sort across multiple ranks
    if (records.size() > 1) {
        std::stable_sort(records.begin(), records.end(),
            [](const ubpf_event_record_header *a, const ubpf_event_record_header *b) {
                return a->timestamp_ns < b->timestamp_ns;
            });
    }

    // 3. Append formatted strings to batch buffer
    for (const auto *hdr : records) {
        const char *payload = reinterpret_cast<const char *>(hdr) + sizeof(ubpf_event_record_header);
        uint32_t payload_len = hdr->payload_len;

        if (hdr->event_type == 1) { // Formatted printf text
            if (payload_len > 0) {
                batch_buffer_.insert(batch_buffer_.end(), payload, payload + payload_len);
                if (batch_buffer_.back() != '\n') {
                    batch_buffer_.push_back('\n');
                }
            }
        } else { // Binary event summary
            char line_buf[128];
            int n = snprintf(line_buf, sizeof(line_buf),
                             "[Rank %u, EventType %u, TS %lu ns] Payload: %u bytes\n",
                             hdr->rank, hdr->event_type, (unsigned long)hdr->timestamp_ns, payload_len);
            if (n > 0) {
                batch_buffer_.insert(batch_buffer_.end(), line_buf, line_buf + n);
            }
        }
    }

    // 4. Trigger flush if buffer threshold reached
    if (batch_buffer_.size() >= buffer_threshold_bytes_) {
        fwrite(batch_buffer_.data(), 1, batch_buffer_.size(), stdout);
        fflush(stdout);
        batch_buffer_.clear();
        last_flush_ns_ = get_now_ns();
    }
}

void ubpf_micro_streamer::check_timer_and_flush() {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (batch_buffer_.empty()) {
        return;
    }

    uint64_t now_ns = get_now_ns();
    if (now_ns - last_flush_ns_ >= flush_timeout_ns_) {
        fwrite(batch_buffer_.data(), 1, batch_buffer_.size(), stdout);
        fflush(stdout);
        batch_buffer_.clear();
        last_flush_ns_ = now_ns;
    }
}

void ubpf_micro_streamer::flush() {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (!batch_buffer_.empty()) {
        fwrite(batch_buffer_.data(), 1, batch_buffer_.size(), stdout);
        fflush(stdout);
        batch_buffer_.clear();
        last_flush_ns_ = get_now_ns();
    }
}

} // namespace hpc
} // namespace bpftime

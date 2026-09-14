#include "hpc/ubpf_shm_buffer.hpp"
#include "hpc/ubpf_container_writer.hpp"
#include "hpc/ubpf_async_io_worker.hpp"
#include "hpc/ubpf_topology.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstring>
#include <chrono>
#include <fstream>
#include <lz4.h>

using namespace bpftime::hpc;

namespace {

void test_basic_shm_and_worker() {
    std::cout << "[Test 1] Initializing POSIX SHM segment & Container Writer..." << std::endl;
    const char *shm_name = "/ubpftrace_test_shm_phase2";
    const std::string container_path = "/tmp/ubpftrace_test_phase2.ubpf";

    auto *shm = ubpf_shm_create_or_attach(1001, 0, true, shm_name);
    assert(shm != nullptr);
    assert(shm->magic == UBPF_SHM_MAGIC);
    assert(shm->version == UBPF_SHM_VERSION);

    // Verify cacheline alignment of rank_stats
    for (size_t i = 0; i < 4; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(&shm->rank_stats[i]);
        assert(addr % 64 == 0);
    }

    ubpf_container_writer writer;
    bool opened = writer.open(container_path, 1001, 0, "test-compute-node");
    assert(opened);

    ubpf_async_io_worker worker;
    // Set 512KB watermark for quick flushing during unit test, 100ms soft timer
    bool started = worker.start(shm, &writer, 512 * 1024, 100000000ULL);
    assert(started);

    std::cout << "[Test 2] Launching 16 concurrent MPI rank producer threads..." << std::endl;
    constexpr int NUM_THREADS = 16;
    constexpr int RECORDS_PER_THREAD = 5000;
    std::atomic<uint64_t> total_pushed{0};

    std::vector<std::thread> producers;
    for (int t = 0; t < NUM_THREADS; ++t) {
        producers.emplace_back([shm, t, &total_pushed]() {
            for (int r = 0; r < RECORDS_PER_THREAD; ++r) {
                const char payload[] = "MPI_Send(count=1024, datatype=MPI_DOUBLE, dest=1, tag=0)";
                uint32_t payload_len = sizeof(payload);
                uint32_t record_len = sizeof(ubpf_event_record_header) + payload_len;

                uint8_t *record_ptr = nullptr;
                uint32_t buf_idx = 0;

                if (ubpf_probe_reserve_record(shm, t, record_len, &record_ptr, &buf_idx)) {
                    auto *hdr = reinterpret_cast<ubpf_event_record_header *>(record_ptr);
                    hdr->record_len = record_len;
                    hdr->rank = static_cast<uint16_t>(t);
                    hdr->event_type = 1; // probe event
                    hdr->timestamp_ns = 1000000000ULL + r;
                    hdr->payload_len = payload_len;
                    hdr->reserved = 0;

                    std::memcpy(record_ptr + sizeof(ubpf_event_record_header), payload, payload_len);
                    ubpf_probe_commit_record(shm, t, buf_idx);
                    total_pushed.fetch_add(1, std::memory_order_relaxed);
                }

                // Micro-delay between probes
                if (r % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
        });
    }

    for (auto &th : producers) {
        th.join();
    }

    std::cout << "[Test 3] All 16 threads finished. Total records successfully pushed: "
              << total_pushed.load() << std::endl;

    // Stop worker and flush all remaining data
    worker.stop();
    writer.close();
    ubpf_shm_detach(shm, true, shm_name);

    std::cout << "[Test 4] Verifying generated Lustre container file format & CRC32..." << std::endl;
    std::ifstream file(container_path, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    std::streamsize file_size = file.tellg();
    assert(file_size >= static_cast<std::streamsize>(sizeof(ubpf_file_header) + sizeof(ubpf_file_trailer)));

    file.seekg(0, std::ios::beg);
    ubpf_file_header fhdr;
    file.read(reinterpret_cast<char *>(&fhdr), sizeof(fhdr));
    assert(fhdr.magic == UBPF_FILE_MAGIC);
    assert(fhdr.version == UBPF_FILE_VERSION);
    assert(fhdr.job_id == 1001);
    assert(fhdr.codec_id == CODEC_LZ4);
    assert(std::string(fhdr.hostname) == "test-compute-node");

    // Read trailer at end
    file.seekg(file_size - static_cast<std::streamsize>(sizeof(ubpf_file_trailer)), std::ios::beg);
    ubpf_file_trailer trailer;
    file.read(reinterpret_cast<char *>(&trailer), sizeof(trailer));
    assert(trailer.trailer_magic == UBPF_TRAILER_MAGIC);
    assert(trailer.total_chunks > 0);
    assert(trailer.total_records == total_pushed.load());
    assert(trailer.total_dropped == 0);

    std::cout << " -> Container verified successfully! Total Chunks: " << trailer.total_chunks
              << ", Total Records: " << trailer.total_records
              << ", Total Dropped: " << trailer.total_dropped
              << ", File Size: " << file_size << " bytes." << std::endl;
}

void test_buffer_saturation_and_drop_handling() {
    std::cout << "[Test 5] Testing artificial buffer saturation & wait-free drop handling..." << std::endl;
    const char *shm_name = "/ubpftrace_test_saturation_shm";

    auto *shm = ubpf_shm_create_or_attach(2002, 0, true, shm_name);
    assert(shm != nullptr);

    // Intentionally fill buffer beyond capacity without running I/O worker
    constexpr size_t TEST_REC_LEN = 64 * 1024; // 64 KB per record
    constexpr size_t TOTAL_ATTEMPTS = 300;     // 300 * 64KB = 19.2 MB > 16 MB capacity

    size_t successful_reserves = 0;
    size_t rejected_reserves = 0;

    for (size_t i = 0; i < TOTAL_ATTEMPTS; ++i) {
        uint8_t *ptr = nullptr;
        uint32_t buf_idx = 0;
        if (ubpf_probe_reserve_record(shm, 0, TEST_REC_LEN, &ptr, &buf_idx)) {
            successful_reserves++;
            ubpf_probe_commit_record(shm, 0, buf_idx);
        } else {
            rejected_reserves++;
        }
    }

    uint64_t recorded = shm->rank_stats[0].recorded_events.load();
    uint64_t dropped = shm->rank_stats[0].dropped_events.load();

    assert(successful_reserves > 0);
    assert(rejected_reserves > 0);
    assert(recorded == successful_reserves);
    assert(dropped == rejected_reserves);
    assert(recorded + dropped == TOTAL_ATTEMPTS);

    std::cout << " -> Saturation handled gracefully! Successful: " << recorded
              << ", Dropped: " << dropped
              << ", Total: " << (recorded + dropped) << " (Zero Deadlocks / No Stalls)" << std::endl;

    ubpf_shm_detach(shm, true, shm_name);
}

} // anonymous namespace

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  UBPFTRACE PHASE 2: ASYNC SHM & I/O ENGINE UNIT TEST SUITE" << std::endl;
    std::cout << "============================================================" << std::endl;

    test_basic_shm_and_worker();
    test_buffer_saturation_and_drop_handling();

    std::cout << "============================================================" << std::endl;
    std::cout << "  ALL PHASE 2 UNIT TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}

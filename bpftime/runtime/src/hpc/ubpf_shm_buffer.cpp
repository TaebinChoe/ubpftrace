#include "hpc/ubpf_shm_buffer.hpp"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <iostream>

namespace bpftime {
namespace hpc {

namespace {

std::string get_default_shm_name(uint32_t job_id, uint32_t node_id) {
    return "/ubpftrace_node_" + std::to_string(job_id) + "_" + std::to_string(node_id);
}

} // anonymous namespace

ubpf_node_shm_header *ubpf_shm_create_or_attach(uint32_t job_id, uint32_t node_id,
                                                bool is_creator,
                                                const char *custom_shm_name) {
    std::string shm_name = custom_shm_name ? custom_shm_name : get_default_shm_name(job_id, node_id);
    size_t total_size = sizeof(ubpf_node_shm_header);

    int oflag = O_RDWR;
    if (is_creator) {
        // Creator creates with O_CREAT
        oflag |= O_CREAT;
        // Clean up any stale segment
        shm_unlink(shm_name.c_str());
    }

    int fd = shm_open(shm_name.c_str(), oflag, 0666);
    if (fd < 0) {
        // Fallback: If attach failed because creator hasn't finished, attempt with create
        if (!is_creator) {
            fd = shm_open(shm_name.c_str(), O_RDWR | O_CREAT, 0666);
            if (fd < 0) return nullptr;
        } else {
            return nullptr;
        }
    }

    if (is_creator) {
        if (ftruncate(fd, static_cast<off_t>(total_size)) != 0) {
            close(fd);
            return nullptr;
        }
    }

    void *ptr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED || !ptr) {
        return nullptr;
    }

    auto *hdr = static_cast<ubpf_node_shm_header *>(ptr);

    if (is_creator) {
        std::memset(ptr, 0, total_size);
        hdr->magic = UBPF_SHM_MAGIC;
        hdr->version = UBPF_SHM_VERSION;
        hdr->job_id = job_id;
        hdr->node_id = node_id;
        hdr->num_local_ranks = 0;
        hdr->map_overflow_flag.store(false, std::memory_order_relaxed);
        hdr->active_epoch_buffer.store(0, std::memory_order_release);
    }

    return hdr;
}

void ubpf_shm_detach(ubpf_node_shm_header *shm, bool is_creator,
                     const char *custom_shm_name) {
    if (!shm) return;

    uint32_t job_id = shm->job_id;
    uint32_t node_id = shm->node_id;
    std::string shm_name = custom_shm_name ? custom_shm_name : get_default_shm_name(job_id, node_id);
    size_t total_size = sizeof(ubpf_node_shm_header);

    munmap(static_cast<void *>(shm), total_size);

    if (is_creator) {
        shm_unlink(shm_name.c_str());
    }
}

} // namespace hpc
} // namespace bpftime

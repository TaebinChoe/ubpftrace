#include "hpc/ubpf_topology.hpp"

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/utsname.h>
#include <string>
#include <atomic>

namespace bpftime {
namespace hpc {

namespace {

// Parse integer from environment variable if present
static bool get_env_uint32(const char *var_name, uint32_t &out_val) {
    const char *val = std::getenv(var_name);
    if (val && *val) {
        char *endptr = nullptr;
        long parsed = std::strtol(val, &endptr, 10);
        if (endptr != val && parsed >= 0) {
            out_val = static_cast<uint32_t>(parsed);
            return true;
        }
    }
    return false;
}

// FNV-1a 32-bit hash for hostname fallback
static uint32_t hash_hostname(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

} // anonymous namespace

uint32_t get_mpi_rank() {
    static std::atomic<bool> initialized{false};
    static uint32_t cached_rank = 0;

    if (__builtin_expect(!initialized.load(std::memory_order_relaxed), 0)) {
        uint32_t val = 0;
        if (get_env_uint32("SLURM_PROCID", val) ||
            get_env_uint32("PMI_RANK", val) ||
            get_env_uint32("OMPI_COMM_WORLD_RANK", val) ||
            get_env_uint32("PMIX_RANK", val) ||
            get_env_uint32("ALPS_APP_PE", val) ||
            get_env_uint32("MPI_RANK", val)) {
            cached_rank = val;
        } else {
            cached_rank = 0;
        }
        initialized.store(true, std::memory_order_release);
    }
    return cached_rank;
}

uint32_t get_node_id() {
    static std::atomic<bool> initialized{false};
    static uint32_t cached_node = 0;

    if (__builtin_expect(!initialized.load(std::memory_order_relaxed), 0)) {
        uint32_t val = 0;
        if (get_env_uint32("SLURM_NODEID", val) ||
            get_env_uint32("PMIX_NODEID", val) ||
            get_env_uint32("OMPI_COMM_WORLD_NODE_RANK", val)) {
            cached_node = val;
        } else {
            char hostname[256];
            if (gethostname(hostname, sizeof(hostname)) == 0) {
                hostname[sizeof(hostname) - 1] = '\0';
                cached_node = hash_hostname(hostname);
            } else {
                cached_node = 0;
            }
        }
        initialized.store(true, std::memory_order_release);
    }
    return cached_node;
}

uint32_t get_local_rank() {
    static std::atomic<bool> initialized{false};
    static uint32_t cached_local_rank = 0;

    if (__builtin_expect(!initialized.load(std::memory_order_relaxed), 0)) {
        uint32_t val = 0;
        if (get_env_uint32("SLURM_LOCALID", val) ||
            get_env_uint32("MPI_LOCALRANKID", val) ||
            get_env_uint32("OMPI_COMM_WORLD_LOCAL_RANK", val) ||
            get_env_uint32("PMIX_LOCAL_RANK", val)) {
            cached_local_rank = val;
        } else {
            cached_local_rank = 0;
        }
        initialized.store(true, std::memory_order_release);
    }
    return cached_local_rank;
}

void get_nodename(char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return;

    static std::atomic<bool> initialized{false};
    static char cached_hostname[256] = {0};

    if (__builtin_expect(!initialized.load(std::memory_order_relaxed), 0)) {
        struct utsname uts;
        if (uname(&uts) == 0) {
            std::strncpy(cached_hostname, uts.nodename, sizeof(cached_hostname) - 1);
            cached_hostname[sizeof(cached_hostname) - 1] = '\0';
        } else if (gethostname(cached_hostname, sizeof(cached_hostname) - 1) != 0) {
            std::strncpy(cached_hostname, "unknown_node", sizeof(cached_hostname) - 1);
        }
        cached_hostname[sizeof(cached_hostname) - 1] = '\0';
        initialized.store(true, std::memory_order_release);
    }

    std::strncpy(out_buf, cached_hostname, max_len - 1);
    out_buf[max_len - 1] = '\0';
}

ubpf_node_topology discover_topology() {
    ubpf_node_topology topo;
    topo.rank = get_mpi_rank();
    topo.node_id = get_node_id();
    topo.local_rank = get_local_rank();
    char host[256];
    get_nodename(host, sizeof(host));
    topo.nodename = host;

    uint32_t jid = 0;
    if (get_env_uint32("SLURM_JOB_ID", jid) ||
        get_env_uint32("SLURM_JOBID", jid) ||
        get_env_uint32("PBS_JOBID", jid)) {
        topo.job_id = jid;
    } else {
        topo.job_id = static_cast<uint32_t>(getpid());
    }
    return topo;
}

} // namespace hpc
} // namespace bpftime

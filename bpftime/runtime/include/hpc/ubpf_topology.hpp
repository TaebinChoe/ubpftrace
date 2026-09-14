#ifndef _UBPF_TOPOLOGY_HPP
#define _UBPF_TOPOLOGY_HPP

#include <cstdint>
#include <cstddef>
#include <string>

namespace bpftime {
namespace hpc {

// Returns the global MPI rank ID (e.g. from SLURM_PROCID, PMI_RANK, etc.)
uint32_t get_mpi_rank();

// Returns the physical node ID / hash (e.g. from SLURM_NODEID, PMIX_NODEID, etc.)
uint32_t get_node_id();

// Returns the node-local rank ID (e.g. from SLURM_LOCALID, MPI_LOCALRANKID, etc.)
uint32_t get_local_rank();

// Writes up to max_len bytes of the node hostname into out_buf (null-terminated)
void get_nodename(char *out_buf, size_t max_len);

struct ubpf_node_topology {
    uint32_t job_id;
    uint32_t node_id;
    uint32_t rank;
    uint32_t local_rank;
    std::string nodename;
};

ubpf_node_topology discover_topology();

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_TOPOLOGY_HPP

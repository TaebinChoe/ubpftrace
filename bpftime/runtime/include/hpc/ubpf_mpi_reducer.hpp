#ifndef _UBPF_MPI_REDUCER_HPP
#define _UBPF_MPI_REDUCER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace bpftime {
namespace hpc {

struct RankSummaryStats {
    uint32_t rank;
    uint32_t node_id;
    uint64_t recorded_events;
    uint64_t dropped_events;
};

struct ClusterTraceSummary {
    uint32_t job_id;
    uint32_t total_ranks;
    uint32_t total_nodes;
    uint64_t total_recorded;
    uint64_t total_dropped;
    std::vector<RankSummaryStats> rank_stats;
    std::vector<std::string> container_files;
};

class ubpf_mpi_reducer {
public:
    static ubpf_mpi_reducer &instance();

    // Intercept MPI initialization and duplicate MPI_COMM_WORLD
    void on_mpi_init();

    // Intercept MPI finalization and perform post-mortem reduction
    void on_mpi_finalize();

    bool is_mpi_active() const { return mpi_initialized_; }

private:
    ubpf_mpi_reducer() = default;
    ~ubpf_mpi_reducer() = default;

    bool mpi_initialized_{false};
    bool is_world_rank_zero_{false};
    uint32_t world_rank_{0};
    uint32_t world_size_{1};
    uintptr_t private_comm_{0}; // Private isolated communicator
};

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_MPI_REDUCER_HPP

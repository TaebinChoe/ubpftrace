#include "hpc/ubpf_mpi_reducer.hpp"
#include "hpc/ubpf_agent_manager.hpp"
#include "hpc/ubpf_topology.hpp"

#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>

// Fallback MPI definitions if mpi.h is not included directly
#if __has_include(<mpi.h>)
#include <mpi.h>
#else
typedef int MPI_Comm;
typedef int MPI_Datatype;
typedef int MPI_Op;
#define MPI_COMM_WORLD ((MPI_Comm)0x44000000)
#define MPI_SUCCESS 0
#define MPI_INT ((MPI_Datatype)0x4c000105)
#define MPI_BYTE ((MPI_Datatype)0x4c00010d)
#define MPI_UINT64_T ((MPI_Datatype)0x4c00083e)
#define MPI_SUM ((MPI_Op)0x58000003)
#endif

namespace bpftime {
namespace hpc {

namespace {

using mpi_init_fn = int (*)(int *, char ***);
using mpi_init_thread_fn = int (*)(int *, char ***, int, int *);
using mpi_finalize_fn = int (*)();
using mpi_comm_dup_fn = int (*)(MPI_Comm, MPI_Comm *);
using mpi_comm_free_fn = int (*)(MPI_Comm *);
using mpi_comm_rank_fn = int (*)(MPI_Comm, int *);
using mpi_comm_size_fn = int (*)(MPI_Comm, int *);
using mpi_reduce_fn = int (*)(const void *, void *, int, MPI_Datatype, MPI_Op, int, MPI_Comm);
using mpi_gather_fn = int (*)(const void *, int, MPI_Datatype, void *, int, MPI_Datatype, int, MPI_Comm);

mpi_init_fn real_mpi_init = nullptr;
mpi_init_thread_fn real_mpi_init_thread = nullptr;
mpi_finalize_fn real_mpi_finalize = nullptr;
mpi_comm_dup_fn real_mpi_comm_dup = nullptr;
mpi_comm_free_fn real_mpi_comm_free = nullptr;
mpi_comm_rank_fn real_mpi_comm_rank = nullptr;
mpi_comm_size_fn real_mpi_comm_size = nullptr;
mpi_reduce_fn real_mpi_reduce = nullptr;
mpi_gather_fn real_mpi_gather = nullptr;

void resolve_mpi_symbols() {
    static bool resolved = false;
    if (resolved) return;
    resolved = true;

    real_mpi_init = (mpi_init_fn)dlsym(RTLD_NEXT, "PMPI_Init");
    if (!real_mpi_init) real_mpi_init = (mpi_init_fn)dlsym(RTLD_NEXT, "MPI_Init");

    real_mpi_init_thread = (mpi_init_thread_fn)dlsym(RTLD_NEXT, "PMPI_Init_thread");
    if (!real_mpi_init_thread) real_mpi_init_thread = (mpi_init_thread_fn)dlsym(RTLD_NEXT, "MPI_Init_thread");

    real_mpi_finalize = (mpi_finalize_fn)dlsym(RTLD_NEXT, "PMPI_Finalize");
    if (!real_mpi_finalize) real_mpi_finalize = (mpi_finalize_fn)dlsym(RTLD_NEXT, "MPI_Finalize");

    real_mpi_comm_dup = (mpi_comm_dup_fn)dlsym(RTLD_NEXT, "PMPI_Comm_dup");
    if (!real_mpi_comm_dup) real_mpi_comm_dup = (mpi_comm_dup_fn)dlsym(RTLD_DEFAULT, "PMPI_Comm_dup");
    if (!real_mpi_comm_dup) real_mpi_comm_dup = (mpi_comm_dup_fn)dlsym(RTLD_DEFAULT, "MPI_Comm_dup");

    real_mpi_comm_free = (mpi_comm_free_fn)dlsym(RTLD_NEXT, "PMPI_Comm_free");
    if (!real_mpi_comm_free) real_mpi_comm_free = (mpi_comm_free_fn)dlsym(RTLD_DEFAULT, "PMPI_Comm_free");
    if (!real_mpi_comm_free) real_mpi_comm_free = (mpi_comm_free_fn)dlsym(RTLD_DEFAULT, "MPI_Comm_free");

    real_mpi_comm_rank = (mpi_comm_rank_fn)dlsym(RTLD_NEXT, "PMPI_Comm_rank");
    if (!real_mpi_comm_rank) real_mpi_comm_rank = (mpi_comm_rank_fn)dlsym(RTLD_DEFAULT, "PMPI_Comm_rank");
    if (!real_mpi_comm_rank) real_mpi_comm_rank = (mpi_comm_rank_fn)dlsym(RTLD_DEFAULT, "MPI_Comm_rank");

    real_mpi_comm_size = (mpi_comm_size_fn)dlsym(RTLD_NEXT, "PMPI_Comm_size");
    if (!real_mpi_comm_size) real_mpi_comm_size = (mpi_comm_size_fn)dlsym(RTLD_DEFAULT, "PMPI_Comm_size");
    if (!real_mpi_comm_size) real_mpi_comm_size = (mpi_comm_size_fn)dlsym(RTLD_DEFAULT, "MPI_Comm_size");

    real_mpi_reduce = (mpi_reduce_fn)dlsym(RTLD_NEXT, "PMPI_Reduce");
    if (!real_mpi_reduce) real_mpi_reduce = (mpi_reduce_fn)dlsym(RTLD_DEFAULT, "PMPI_Reduce");
    if (!real_mpi_reduce) real_mpi_reduce = (mpi_reduce_fn)dlsym(RTLD_DEFAULT, "MPI_Reduce");

    real_mpi_gather = (mpi_gather_fn)dlsym(RTLD_NEXT, "PMPI_Gather");
    if (!real_mpi_gather) real_mpi_gather = (mpi_gather_fn)dlsym(RTLD_DEFAULT, "PMPI_Gather");
    if (!real_mpi_gather) real_mpi_gather = (mpi_gather_fn)dlsym(RTLD_DEFAULT, "MPI_Gather");
}

} // anonymous namespace

ubpf_mpi_reducer &ubpf_mpi_reducer::instance() {
    static ubpf_mpi_reducer inst;
    return inst;
}

void ubpf_mpi_reducer::on_mpi_init() {
    resolve_mpi_symbols();

    // 1. Initialize node-local SHM tracing engine
    ubpf_agent_manager::instance().init();

    // 2. Only duplicate communicator if cluster reduction is requested
    const char *enable_reduce = std::getenv("UBPFTRACE_ENABLE_MPI_REDUCE");
    if (enable_reduce && std::string(enable_reduce) == "1" && real_mpi_comm_dup) {
        MPI_Comm private_comm;
        int ret = real_mpi_comm_dup(MPI_COMM_WORLD, &private_comm);
        if (ret == MPI_SUCCESS) {
            private_comm_ = static_cast<uintptr_t>(private_comm);
            mpi_initialized_ = true;

            int rank = 0;
            int size = 1;
            if (real_mpi_comm_rank) real_mpi_comm_rank(private_comm, &rank);
            if (real_mpi_comm_size) real_mpi_comm_size(private_comm, &size);

            world_rank_ = static_cast<uint32_t>(rank);
            world_size_ = static_cast<uint32_t>(size);
            is_world_rank_zero_ = (world_rank_ == 0);
        }
    }
}

void ubpf_mpi_reducer::on_mpi_finalize() {
    auto &mgr = ubpf_agent_manager::instance();
    uint32_t job_id = mgr.get_job_id();
    uint32_t node_id = mgr.get_node_id();
    uint32_t local_rank = mgr.get_local_rank();

    uint64_t local_recorded = 0;
    uint64_t local_dropped = 0;
    auto *shm = mgr.get_shm();
    if (shm && local_rank < MAX_LOCAL_RANKS) {
        local_recorded = shm->rank_stats[local_rank].recorded_events.load(std::memory_order_relaxed);
        local_dropped = shm->rank_stats[local_rank].dropped_events.load(std::memory_order_relaxed);
    }

    // Flush local node SHM buffer & finalize node container
    mgr.shutdown();

    // 2. Perform cluster-wide reduction over isolated communicator if enabled
    if (mpi_initialized_ && private_comm_ != 0 && real_mpi_reduce) {
        MPI_Comm comm = static_cast<MPI_Comm>(private_comm_);

        uint64_t global_recorded = 0;
        uint64_t global_dropped = 0;

        real_mpi_reduce(&local_recorded, &global_recorded, 1, MPI_UINT64_T, MPI_SUM, 0, comm);
        real_mpi_reduce(&local_dropped, &global_dropped, 1, MPI_UINT64_T, MPI_SUM, 0, comm);

        // Gather per-rank statistics on Rank 0
        std::vector<RankSummaryStats> all_rank_stats;
        RankSummaryStats local_stat{
            .rank = world_rank_,
            .node_id = node_id,
            .recorded_events = local_recorded,
            .dropped_events = local_dropped
        };

        if (is_world_rank_zero_) {
            all_rank_stats.resize(world_size_);
        }

        if (real_mpi_gather) {
            real_mpi_gather(&local_stat, sizeof(RankSummaryStats), MPI_BYTE,
                            all_rank_stats.data(), sizeof(RankSummaryStats), MPI_BYTE,
                            0, comm);
        }

        // 3. Emit consolidated summary JSON on Global Rank 0
        if (is_world_rank_zero_) {
            std::string out_dir = ".";
            if (const char *env_dir = std::getenv("UBPFTRACE_OUTPUT_DIR")) {
                out_dir = env_dir;
            } else if (const char *scratch = std::getenv("SCRATCH")) {
                out_dir = scratch;
            }

            std::string summary_path = out_dir + "/ubpftrace_" +
                                      std::to_string(job_id) + "_summary.json";

            std::ofstream out(summary_path);
            if (out.is_open()) {
                out << "{\n"
                    << "  \"job_id\": " << job_id << ",\n"
                    << "  \"total_ranks\": " << world_size_ << ",\n"
                    << "  \"total_recorded_events\": " << global_recorded << ",\n"
                    << "  \"total_dropped_events\": " << global_dropped << ",\n"
                    << "  \"per_rank_stats\": [\n";

                for (size_t r = 0; r < all_rank_stats.size(); ++r) {
                    const auto &st = all_rank_stats[r];
                    out << "    {\"rank\": " << st.rank
                        << ", \"node_id\": " << st.node_id
                        << ", \"recorded\": " << st.recorded_events
                        << ", \"dropped\": " << st.dropped_events << "}"
                        << (r + 1 < all_rank_stats.size() ? ",\n" : "\n");
                }

                out << "  ]\n}\n";
                out.close();
            }
        }

        // 4. Free isolated communicator
        if (real_mpi_comm_free) {
            real_mpi_comm_free(&comm);
            private_comm_ = 0;
        }
    }
}

} // namespace hpc
} // namespace bpftime

// ----------------------------------------------------------------------
// C Symbol Wrappers exported from libbpftime-agent.so
// ----------------------------------------------------------------------

extern "C" {

__attribute__((visibility("default")))
int MPI_Init(int *argc, char ***argv) {
    bpftime::hpc::resolve_mpi_symbols();
    int ret = MPI_SUCCESS;
    if (bpftime::hpc::real_mpi_init) {
        ret = bpftime::hpc::real_mpi_init(argc, argv);
    }
    if (ret == MPI_SUCCESS) {
        bpftime::hpc::ubpf_mpi_reducer::instance().on_mpi_init();
    }
    return ret;
}

__attribute__((visibility("default")))
int MPI_Init_thread(int *argc, char ***argv, int required, int *provided) {
    bpftime::hpc::resolve_mpi_symbols();
    int ret = MPI_SUCCESS;
    if (bpftime::hpc::real_mpi_init_thread) {
        ret = bpftime::hpc::real_mpi_init_thread(argc, argv, required, provided);
    }
    if (ret == MPI_SUCCESS) {
        bpftime::hpc::ubpf_mpi_reducer::instance().on_mpi_init();
    }
    return ret;
}

__attribute__((visibility("default")))
int MPI_Finalize() {
    bpftime::hpc::resolve_mpi_symbols();
    bpftime::hpc::ubpf_mpi_reducer::instance().on_mpi_finalize();
    int ret = MPI_SUCCESS;
    if (bpftime::hpc::real_mpi_finalize) {
        ret = bpftime::hpc::real_mpi_finalize();
    }
    return ret;
}

} // extern "C"

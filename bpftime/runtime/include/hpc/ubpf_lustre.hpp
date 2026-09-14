#ifndef _UBPF_LUSTRE_HPP
#define _UBPF_LUSTRE_HPP

#include <cstdint>
#include <cstddef>

namespace bpftime {
namespace hpc {

// Given an open file descriptor on a Lustre filesystem and a byte offset,
// queries or uses the cached stripe layout to compute the target OST index (obdidx).
// Returns the target OST ID (>= 0), or -1 on error / non-Lustre filesystem.
int32_t get_lustre_ost(int fd, uint64_t offset);

// Clears cached layout for a specific file descriptor (e.g. on close).
void clear_lustre_layout_cache(int fd);

// Flushes the entire layout cache.
void reset_lustre_layout_cache();

} // namespace hpc
} // namespace bpftime

#endif // _UBPF_LUSTRE_HPP

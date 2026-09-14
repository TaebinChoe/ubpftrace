#include "hpc/ubpf_lustre.hpp"

#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <cstring>

#if __has_include(<linux/lustre/lustre_user.h>)
#include <linux/lustre/lustre_user.h>
#else
// Fallback definitions if Lustre kernel headers are not present in standard path
#define LOV_USER_MAGIC_V1 0x0BD10BD0
#define LOV_USER_MAGIC_V3 0x0BD30BD0
#define LOV_USER_MAGIC_SPECIFIC 0x0BD50BD0
#define LL_IOC_LOV_GETSTRIPE _IOWR('f', 154, long)

struct ost_id {
    union {
        struct {
            uint64_t oi_id;
            uint64_t oi_seq;
        } oi;
        uint64_t oi_dummy;
    };
};

struct lov_user_ost_data_v1 {
    struct ost_id l_ost_oi;
    uint32_t l_ost_gen;
    uint32_t l_ost_idx;
};

struct lov_user_md_v1 {
    uint32_t lmm_magic;
    uint32_t lmm_pattern;
    struct ost_id lmm_oi;
    uint32_t lmm_stripe_size;
    uint16_t lmm_stripe_count;
    uint16_t lmm_layout_gen;
    struct lov_user_ost_data_v1 lmm_objects[0];
};

struct lov_user_md_v3 {
    uint32_t lmm_magic;
    uint32_t lmm_pattern;
    struct ost_id lmm_oi;
    uint32_t lmm_stripe_size;
    uint16_t lmm_stripe_count;
    uint16_t lmm_layout_gen;
    char lmm_pool_name[16];
    struct lov_user_ost_data_v1 lmm_objects[0];
};
#endif

namespace bpftime {
namespace hpc {

namespace {

struct LustreLayoutEntry {
    uint32_t stripe_size = 0;
    uint32_t stripe_count = 0;
    std::vector<uint32_t> ost_indices;
    std::chrono::steady_clock::time_point last_checked;
    bool is_valid = false;
};

class LustreLayoutCache {
public:
    static LustreLayoutCache &instance() {
        static LustreLayoutCache cache;
        return cache;
    }

    int32_t resolve_ost(int fd, uint64_t offset) {
        if (fd < 0) return -1;

        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(fd);
            if (it != cache_.end()) {
                // If entry was queried recently (< 30 seconds)
                if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_checked).count() < 30) {
                    if (!it->second.is_valid || it->second.stripe_size == 0 || it->second.stripe_count == 0) {
                        return -1;
                    }
                    uint64_t s_idx = (offset / it->second.stripe_size) % it->second.stripe_count;
                    if (s_idx < it->second.ost_indices.size()) {
                        return static_cast<int32_t>(it->second.ost_indices[s_idx]);
                    }
                    return -1;
                }
            }
        }

        // Query file layout via ioctl
        alignas(8) char buf[8192] = {0};
        auto *lum = reinterpret_cast<struct lov_user_md_v3 *>(buf);
        lum->lmm_magic = LOV_USER_MAGIC_V3;
        lum->lmm_stripe_count = (sizeof(buf) - sizeof(struct lov_user_md_v3)) / sizeof(struct lov_user_ost_data_v1);

        int rc = ioctl(fd, LL_IOC_LOV_GETSTRIPE, lum);
        LustreLayoutEntry entry;
        entry.last_checked = now;

        if (rc == 0 && lum->lmm_stripe_size > 0 && lum->lmm_stripe_count > 0) {
            entry.stripe_size = lum->lmm_stripe_size;
            entry.stripe_count = lum->lmm_stripe_count;
            entry.is_valid = true;

            const struct lov_user_ost_data_v1 *objects = nullptr;
            if (lum->lmm_magic == LOV_USER_MAGIC_V3 || lum->lmm_magic == LOV_USER_MAGIC_SPECIFIC) {
                objects = lum->lmm_objects;
            } else {
                auto *v1 = reinterpret_cast<const struct lov_user_md_v1 *>(buf);
                objects = v1->lmm_objects;
            }

            entry.ost_indices.reserve(entry.stripe_count);
            for (uint32_t i = 0; i < entry.stripe_count; ++i) {
                entry.ost_indices.push_back(objects[i].l_ost_idx);
            }
        } else {
            entry.is_valid = false;
        }

        int32_t result_ost = -1;
        if (entry.is_valid && entry.stripe_size > 0 && entry.stripe_count > 0) {
            uint64_t s_idx = (offset / entry.stripe_size) % entry.stripe_count;
            if (s_idx < entry.ost_indices.size()) {
                result_ost = static_cast<int32_t>(entry.ost_indices[s_idx]);
            }
        }

        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cache_[fd] = std::move(entry);
        }

        return result_ost;
    }

    void clear(int fd) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.erase(fd);
    }

    void reset() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.clear();
    }

private:
    std::mutex cache_mutex_;
    std::unordered_map<int, LustreLayoutEntry> cache_;
};

} // anonymous namespace

int32_t get_lustre_ost(int fd, uint64_t offset) {
    return LustreLayoutCache::instance().resolve_ost(fd, offset);
}

void clear_lustre_layout_cache(int fd) {
    LustreLayoutCache::instance().clear(fd);
}

void reset_lustre_layout_cache() {
    LustreLayoutCache::instance().reset();
}

} // namespace hpc
} // namespace bpftime

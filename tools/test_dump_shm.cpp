#include <iostream>
#include <vector>
#include <string>
#include "bpftime_shm.hpp"
#include "bpftime_shm_internal.hpp"
#include "handler/map_handler.hpp"
#include "handler/handler_manager.hpp"

int main() {
    try {
        ::bpftime_initialize_global_shm(bpftime::shm_open_type::SHM_OPEN_ONLY);
        const auto *mgr = bpftime::shm_holder.global_shared_memory.get_manager();
        if (!mgr) {
            std::cout << "Manager is null!" << std::endl;
            return 1;
        }
        std::cout << "Manager size: " << mgr->size() << std::endl;
        int alloc_count = 0;
        int map_count = 0;
        for (size_t i = 0; i < mgr->size(); ++i) {
            if (!mgr->is_allocated(i)) continue;
            alloc_count++;
            const auto &handler = mgr->get_handler(i);
            std::cout << "Slot " << i << " holds index: " << handler.index() << std::endl;
            if (std::holds_alternative<bpftime::bpf_map_handler>(handler)) {
                map_count++;
                const auto &map = std::get<bpftime::bpf_map_handler>(handler);
                std::cout << "  Map name: '" << map.name.c_str() << "' type: " << (int)map.type
                          << " key_sz: " << map.get_key_size()
                          << " val_sz: " << map.get_value_size()
                          << " max_ent: " << map.get_max_entries() << std::endl;
            }
        }
        std::cout << "Total allocated slots: " << alloc_count << ", Maps: " << map_count << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}

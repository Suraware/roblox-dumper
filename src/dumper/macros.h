#include "dumper.h"
#include "process/helpers/helpers.h"
#include <spdlog/spdlog.h>

#define FIND_AND_ADD_OFFSET(base_addr, namespace_name, type, name, target, max, align)             \
    do {                                                                                           \
        const auto offset = process::helpers::find_offset<type>(base_addr, target, max, align);    \
        if (!offset) {                                                                             \
            spdlog::warn("Failed to find {} in {} (continuing)", #name, #namespace_name);          \
            break;                                                                                 \
        }                                                                                          \
        dumper::g_dumper.add_offset(#namespace_name, #name, *offset);                              \
    } while (0)
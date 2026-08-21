#include "internal_writer.h"
#include "config.h"
#include "dumper/dumper.h"
#include "process/process.h"
#include <algorithm>
#include <format>

namespace dumper::writer {

    auto InternalWriter::generate_header_comment(std::chrono::milliseconds elapsed_time)
        -> std::string {
        const auto version = process::g_process.get_version();
        std::string version_str = version ? *version : "unknown";

        std::string comment = "/*\n";
        comment += " * INTERNAL OFFSETS - in-process use only\n";
        comment += " * Dumped With: " + std::string(PROJECT_NAME) + " " +
                   std::string(PROJECT_VERSION) + "\n";
        comment += " * Roblox Version: " + version_str + "\n";
        comment += " * Time Taken: " + std::to_string(elapsed_time.count()) + " ms\n";
        comment += " */\n\n";
        return comment;
    }

    auto InternalWriter::generate_content() -> std::string {
        std::string content = "#pragma once\n";
        content += "#include <cstdint>\n\n";
        content += "// clang-format off\n";
        content += "namespace internal {\n";

        for (const auto& [ns_name, entries] : g_dumper.m_internal_offsets) {
            if (entries.empty()) continue;

            auto sorted = entries;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.name < b.name; });

            content += "    namespace " + ns_name + " {\n";
            for (const auto& e : sorted) {
                content += std::format("        inline constexpr uintptr_t {} = 0x{:X};\n",
                                       e.name, e.offset);
            }
            content += "    }\n\n";
        }

        content += "} // namespace internal\n";
        return content;
    }

} // namespace dumper::writer

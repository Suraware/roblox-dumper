#include "config.h"
#include "dumper/dumper.h"
#include "writer/writer.h"
#include <chrono>
#include <format>
#include <iostream>
#include <logger/logger.h>
#include <process/process.h>
#include <spdlog/spdlog.h>

auto main() -> int {
    logger::initialize();

    const auto title = std::format("{} {} (Linux Port)", PROJECT_NAME, PROJECT_VERSION);

    spdlog::info("{} ported from nopjo/roblox-dumper", title);
    spdlog::info("Github: https://github.com/nopjo/roblox-dumper\n");

    if (!process::g_process.attach("sober")) {
        spdlog::error("Failed to attach to Sober. Make sure Sober is running.");
        spdlog::error("Try: sudo setcap cap_sys_ptrace+eip ./roblox-dumper");
        spdlog::error("Or run as root.");
        return 1;
    }

    spdlog::info("Attached to Sober. PID: {}\n", process::g_process.get_pid());

    const auto start_time = std::chrono::steady_clock::now();
    dumper::g_dumper.start();

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    spdlog::info("Finished in {} ms ({:.2f} seconds)", elapsed.count(), elapsed.count() / 1000.0);

    dumper::writer::g_header_writer.write("offsets", elapsed);
    dumper::writer::g_json_writer.write("offsets", elapsed);
    dumper::writer::g_python_writer.write("offsets", elapsed);
    dumper::writer::g_csharp_writer.write("offsets", elapsed);

    logger::print_error_summary();

    return 0;
}

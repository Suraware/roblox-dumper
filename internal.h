/*
 * INTERNAL OFFSETS - in-process use only
 * Dumped With: roblox-dumper 3.0
 * Roblox Version: 199da195b4fd47
 * Time Taken: 1774 ms
 */

#pragma once
#include <cstdint>

// clang-format off
namespace internal {
    namespace GCoPage {
        inline constexpr uintptr_t Mark = 0x3;
        inline constexpr uintptr_t Next = 0x18;
    }

    namespace Functions {
        inline constexpr uintptr_t ClassNameToDescriptor = 0x2187550;
        inline constexpr uintptr_t InstanceCreateCore = 0x2409CCE;
        inline constexpr uintptr_t InstanceNew = 0x26F7744;
        inline constexpr uintptr_t LuaDCall = 0x26DFAB4;
        inline constexpr uintptr_t LuaResume = 0x65A4DA0;
    }

    namespace global_State {
        inline constexpr uintptr_t GCGoal = 0x38;
        inline constexpr uintptr_t GCMulParam = 0x40;
        inline constexpr uintptr_t GCPageListHead = 0x300;
        inline constexpr uintptr_t GCPaused = 0x48;
        inline constexpr uintptr_t GCRunning = 0x59;
        inline constexpr uintptr_t GCStepMul = 0x3C;
        inline constexpr uintptr_t TmCache = 0x328;
        inline constexpr uintptr_t TmNames = 0x440;
        inline constexpr uintptr_t TotalBytes = 0x50;
    }

    namespace TValue {
        inline constexpr uintptr_t Tag = 0xC;
        inline constexpr uintptr_t Value = 0x0;
    }

    namespace lua_State {
        inline constexpr uintptr_t global = 0x20;
        inline constexpr uintptr_t stack = 0x28;
        inline constexpr uintptr_t stack_end = 0x10;
        inline constexpr uintptr_t top = 0x8;
    }

    namespace Luau {
        inline constexpr uintptr_t GlobalStatePointer = 0x6F81040;
        inline constexpr uintptr_t LuaDCheckStack = 0x26EC197;
    }

} // namespace internal

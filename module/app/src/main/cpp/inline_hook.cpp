#include "inline_hook.h"
#include "log.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <cstdio>

namespace hook {

static constexpr int kPatchWords = 4;              // 16 字节
static constexpr int kPatchBytes = kPatchWords * 4;

// AArch64 编码常量
static constexpr uint32_t kLdrX16_8  = 0x58000050u; // LDR x16, #8
static constexpr uint32_t kBrX16     = 0xD61F0200u; // BR  x16

// ---------------------------------------------------------------------------
// 判断一条指令是否 PC 相对（这类指令复制到 trampoline 后语义会变）
// ---------------------------------------------------------------------------
static bool is_pc_relative(uint32_t insn) {
    if ((insn & 0x7C000000u) == 0x14000000u) return true; // B / BL
    if ((insn & 0x7E000000u) == 0x34000000u) return true; // CBZ / CBNZ
    if ((insn & 0x7E000000u) == 0x36000000u) return true; // TBZ / TBNZ
    if ((insn & 0x3B000000u) == 0x18000000u) return true; // LDR/LDRSW (literal)
    if ((insn & 0x1F000000u) == 0x10000000u) return true; // ADR / ADRP
    return false;
}

static size_t page_size() {
    static size_t ps = (size_t)sysconf(_SC_PAGESIZE);
    return ps;
}

static void* page_begin(void* p) {
    return (void*)((uintptr_t)p & ~(uintptr_t)(page_size() - 1));
}

// ---------------------------------------------------------------------------
// trampoline：16 字节原指令 + LDR/BR 跳回 target+16
// ---------------------------------------------------------------------------
static void* make_trampoline(void* target) {
    size_t sz = (size_t)page_size();
    void* mem = mmap(nullptr, sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        LOGE("mmap trampoline failed: %s", strerror(errno));
        return nullptr;
    }
    uint32_t* t = (uint32_t*)mem;
    memcpy(t, target, kPatchBytes);

    t[4] = kLdrX16_8;
    t[5] = kBrX16;
    *(uint64_t*)(t + 6) = (uint64_t)((uint8_t*)target + kPatchBytes);

    __builtin___clear_cache((char*)mem, (char*)mem + 64);
    return mem;
}

bool install(void* target, void* replace, void** orig) {
    if (!target || !replace || !orig) return false;

    uint32_t* p = (uint32_t*)target;
    for (int i = 0; i < kPatchWords; ++i) {
        if (is_pc_relative(p[i])) {
            LOGW("hook rejected: PC-relative insn 0x%08x at %p+%d",
                 p[i], target, i * 4);
            return false;
        }
    }

    void* tramp = make_trampoline(target);
    if (!tramp) return false;

    // 改页权限 → 写入 16 字节跳转桩 → 恢复 RX
    void* pg = page_begin(target);
    size_t len = ((uintptr_t)target + kPatchBytes + page_size() - 1) & ~(page_size() - 1);
    len -= (uintptr_t)pg;
    if (mprotect(pg, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("mprotect RWX failed at %p: %s", pg, strerror(errno));
        munmap(tramp, page_size());
        return false;
    }

    uint32_t patch[kPatchWords];
    patch[0] = kLdrX16_8;
    patch[1] = kBrX16;
    *(uint64_t*)(patch + 2) = (uint64_t)replace;
    memcpy(target, patch, kPatchBytes);

    __builtin___clear_cache((char*)target, (char*)target + kPatchBytes);
    mprotect(pg, len, PROT_READ | PROT_EXEC);

    *orig = tramp;
    LOGI("hooked %p -> %p (tramp %p)", target, replace, tramp);
    return true;
}

bool install_single(void* target, void* replace, void** orig, uint32_t expected) {
    if (!target || !replace || !orig || ((uintptr_t)target & 3u)) return false;
    uint32_t* instruction = static_cast<uint32_t*>(target);
    if (__atomic_load_n(instruction, __ATOMIC_ACQUIRE) != expected || is_pc_relative(expected)) {
        LOGW("single hook rejected: unexpected instruction at %p", target);
        return false;
    }
    struct BridgePage { uint8_t* base; size_t used; };
    static BridgePage pages[32] = {};
    static size_t pageCount = 0;
    void* bridge = MAP_FAILED;
    const uintptr_t center = (uintptr_t)page_begin(target);
    for (size_t i = 0; i < pageCount; ++i) {
        if (pages[i].used + 64 > page_size()) continue;
        uint8_t* slot = pages[i].base + pages[i].used;
        int64_t delta = (int64_t)((uintptr_t)slot + 32) - (int64_t)(uintptr_t)target;
        if (delta >= -0x8000000 && delta < 0x8000000) {
            bridge = slot;
            pages[i].used += 64;
            break;
        }
    }
    if (bridge == MAP_FAILED && pageCount < sizeof(pages) / sizeof(pages[0])) {
        FILE* maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            uintptr_t previous = center > 0x8000000 ? center - 0x8000000 : 0;
            const uintptr_t limit = center + 0x7fff000;
            while (fgets(line, sizeof(line), maps)) {
                unsigned long begin = 0, end = 0;
                if (sscanf(line, "%lx-%lx", &begin, &end) != 2) continue;
                if (begin > previous) {
                    uintptr_t low = (previous + page_size() - 1) & ~(page_size() - 1);
                    uintptr_t high = (uintptr_t)begin < limit ? (uintptr_t)begin : limit;
                    if (high >= low && high - low >= page_size()) {
                        uintptr_t hint = center < low ? low : (center + page_size() <= high ? center : (high - page_size()) & ~(page_size() - 1));
                        void* candidate = mmap((void*)hint, page_size(), PROT_READ | PROT_WRITE | PROT_EXEC,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                        if (candidate != MAP_FAILED) {
                            int64_t delta = (int64_t)((uintptr_t)candidate + 32) - (int64_t)(uintptr_t)target;
                            if (delta >= -0x8000000 && delta < 0x8000000) {
                                bridge = candidate;
                                pages[pageCount++] = {(uint8_t*)candidate, 64};
                                break;
                            }
                            munmap(candidate, page_size());
                        }
                    }
                }
                if ((uintptr_t)end > previous) previous = (uintptr_t)end;
                if (previous >= limit) break;
            }
            fclose(maps);
        }
    }
    if (bridge == MAP_FAILED) {
        LOGW("single hook: no nearby executable memory for %p", target);
        return false;
    }
    uint32_t* code = static_cast<uint32_t*>(bridge);
    code[0] = expected;
    code[1] = 0x58000070u;
    code[2] = kBrX16;
    code[3] = 0xd503201fu;
    *(uint64_t*)(code + 4) = (uint64_t)((uintptr_t)target + 4);
    code[8] = kLdrX16_8;
    code[9] = kBrX16;
    *(uint64_t*)(code + 10) = (uint64_t)replace;
    __builtin___clear_cache((char*)bridge, (char*)bridge + 48);
    void* page = page_begin(target);
    if (mprotect(page, page_size(), PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("single hook mprotect failed: %s", strerror(errno));
        return false;
    }
    int64_t delta = (int64_t)((uintptr_t)bridge + 32) - (int64_t)(uintptr_t)target;
    uint32_t branch = 0x14000000u | ((uint32_t)(delta / 4) & 0x03ffffffu);
    __atomic_store_n(orig, bridge, __ATOMIC_RELEASE);
    __atomic_store_n(instruction, branch, __ATOMIC_RELEASE);
    __builtin___clear_cache((char*)instruction, (char*)(instruction + 1));
    if (mprotect(page, page_size(), PROT_READ | PROT_EXEC) != 0)
        LOGE("single hook mprotect restore failed: %s", strerror(errno));
    LOGI("single hook installed at %p", target);
    return true;
}

bool set_constant_bool(void* target, bool enabled) {
    if (!target || ((uintptr_t)target & 3u)) return false;
    uint32_t* p = static_cast<uint32_t*>(target);
    constexpr uint32_t kReturnFalse = 0x2a1f03e0u;
    constexpr uint32_t kReturnTrue = 0x52800020u;
    constexpr uint32_t kReturn = 0xd65f03c0u;
    uint32_t current = __atomic_load_n(p, __ATOMIC_ACQUIRE);
    if ((current != kReturnFalse && current != kReturnTrue) || p[1] != kReturn) {
        LOGW("constant bool patch rejected: unexpected instructions at %p", target);
        return false;
    }
    uint32_t desired = enabled ? kReturnTrue : kReturnFalse;
    if (current == desired) return true;
    void* page = page_begin(target);
    if (mprotect(page, page_size(), PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("constant bool mprotect failed: %s", strerror(errno));
        return false;
    }
    __atomic_store_n(p, desired, __ATOMIC_RELEASE);
    __builtin___clear_cache((char*)p, (char*)(p + 1));
    if (mprotect(page, page_size(), PROT_READ | PROT_EXEC) != 0)
        LOGE("constant bool mprotect restore failed: %s", strerror(errno));
    LOGI("constant bool at %p set to %s", target, enabled ? "true" : "false");
    return true;
}

bool restore(void* target) {
    if (!target) return false;
    // 第一版不保留原始字节副本，restore 只做占位（热重载场景需要时会扩展）
    LOGW("restore() not implemented, target %p left hooked", target);
    return false;
}

} // namespace hook

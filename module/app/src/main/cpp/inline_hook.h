#pragma once
#include <cstdint>

namespace hook {

// 安装一个 inline hook。成功返回 true，并把 trampoline（原函数前 4 条指令 + 跳回）
// 地址写进 *orig，调用方通过 orig 调用原逻辑。
//
// 限制（第一版）：只支持 AArch64；被 hook 函数前 16 字节内若含 PC 相对指令
// （B/BL/CBZ/TBZ/LDR literal/ADR/ADRP）则拒绝安装，返回 false。
// 实践中 Unity/IL2CPP 生成的函数序言几乎都是 stp/sub/str，命中率很高。
bool install(void* target, void* replace, void** orig);

bool install_single(void* target, void* replace, void** orig, uint32_t expected);

bool set_constant_bool(void* target, bool enabled);

// 恢复（把原始 16 字节写回）。仅用于测试/热重载。
bool restore(void* target);

} // namespace hook

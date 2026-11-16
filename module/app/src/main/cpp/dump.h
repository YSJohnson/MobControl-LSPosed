#pragma once

namespace dump {
// 把当前 IL2CPP 运行时里所有程序集/类/字段/方法转储到 outDir 下的文本文件。
bool write_all(const char* outDir);

// 只转储名字包含 keyword 的类（用于现场定位，比全量快很多）。
void write_filtered(const char* outDir, const char* keyword);
}

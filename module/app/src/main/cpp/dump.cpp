#include "dump.h"
#include "il2cpp_api.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <string>

static constexpr int FieldStatic = 0x0010;

namespace {

std::string TypeOf(const Il2CppType* t) {
    if (!t || !ic::type_get_name) return "?";
    char* s = ic::type_get_name(t);
    return s ? std::string(s) : std::string("?");
}

// 判断类是否值得写：过滤编译器生成物，输出体积能砍掉一半
bool IsNoise(const char* name) {
    if (!name) return true;
    if (strstr(name, "<>c")) return true;          // lambda 闭包
    if (strstr(name, "<>f__AnonymousType")) return true;
    if (strstr(name, "__StaticArrayInitTypeSize")) return true;
    if (strstr(name, "__Il2CppArrayBounds")) return true;
    return false;
}

void WriteClass(FILE* fp, Il2CppClass* k) {
    const char* name = ic::class_get_name(k);
    if (IsNoise(name)) return;

    std::string fq = ic::full_name(k);
    Il2CppClass* parent = ic::class_get_parent(k);
    std::fprintf(fp, "  [Class] %s", fq.c_str());
    if (parent) std::fprintf(fp, " : %s", ic::full_name(parent).c_str());
    std::fprintf(fp, "\n");

    void* it = nullptr;
    FieldInfo* f;
    while ((f = ic::class_get_fields(k, &it))) {
        uint32_t fl = ic::field_get_flags ? ic::field_get_flags(f) : 0;
        std::fprintf(fp, "    F %-32s %-28s off=%-4zu%s\n",
                     ic::field_get_name(f),
                     TypeOf(ic::field_get_type(f)).c_str(),
                     ic::field_get_offset(f),
                     (fl & FieldStatic) ? " static" : "");
    }

    void* it2 = nullptr;
    const MethodInfo* m;
    while ((m = ic::class_get_methods(k, &it2))) {
        const char* mn = ic::method_get_name(m);
        if (!mn) continue;
        void* p = *(void**)m;   // MethodInfo->methodPointer
        std::fprintf(fp, "    M %-32s /%-3u -> %-24s @%p\n",
                     mn, ic::method_get_param_count(m),
                     TypeOf(ic::method_get_return_type(m)).c_str(), p);
    }
}

} // namespace

namespace dump {

bool write_all(const char* outDir) {
    if (!outDir) return false;
    std::string path = std::string(outDir) + "/il2cpp_dump.txt";
    FILE* fp = std::fopen(path.c_str(), "w");
    if (!fp) {
        LOGE("dump: cannot open %s", path.c_str());
        return false;
    }

    size_t n = 0;
    const Il2CppAssembly** as = ic::domain_get_assemblies(ic::domain_get(), &n);
    std::fprintf(fp, "# il2cpp runtime dump — %zu assembly(ies)\n", n);

    size_t totalClasses = 0;
    for (size_t i = 0; i < n; ++i) {
        const Il2CppImage* img = ic::assembly_get_image(as[i]);
        if (!img) continue;
        const char* imgName = ic::image_get_name(img);
        size_t cc = ic::image_get_class_count(img);
        std::fprintf(fp, "\n[Image] %s  (%zu classes)\n",
                     imgName ? imgName : "?", cc);
        totalClasses += cc;

        for (size_t j = 0; j < cc; ++j) {
            Il2CppClass* k = ic::image_get_class(img, j);
            if (k) WriteClass(fp, k);
        }
    }

    bool success = std::ferror(fp) == 0;
    if (std::fclose(fp) != 0) success = false;
    if (!success) {
        LOGE("dump: write failed: %s", path.c_str());
        return false;
    }
    LOGI("dump written: %s (%zu classes)", path.c_str(), totalClasses);
    return true;
}

void write_filtered(const char* outDir, const char* keyword) {
    if (!outDir || !keyword) return;
    std::string path = std::string(outDir) + "/il2cpp_" + keyword + ".txt";
    FILE* fp = std::fopen(path.c_str(), "w");
    if (!fp) return;

    size_t n = 0;
    const Il2CppAssembly** as = ic::domain_get_assemblies(ic::domain_get(), &n);
    int hits = 0;
    for (size_t i = 0; i < n; ++i) {
        const Il2CppImage* img = ic::assembly_get_image(as[i]);
        if (!img) continue;
        size_t cc = ic::image_get_class_count(img);
        for (size_t j = 0; j < cc; ++j) {
            Il2CppClass* k = ic::image_get_class(img, j);
            if (!k) continue;
            std::string fq = ic::full_name(k);
            if (fq.find(keyword) == std::string::npos) continue;
            WriteClass(fp, k);
            ++hits;
        }
    }
    std::fclose(fp);
    LOGI("filtered dump: %s (%d classes matching '%s')", path.c_str(), hits, keyword);
}

} // namespace dump

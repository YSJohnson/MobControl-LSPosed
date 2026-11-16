#include "il2cpp_api.h"
#include "log.h"
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <string>

namespace ic {

pfn_domain_get              domain_get              = nullptr;
pfn_domain_get_assemblies   domain_get_assemblies   = nullptr;
pfn_assembly_get_image      assembly_get_image      = nullptr;
pfn_image_get_name          image_get_name          = nullptr;
pfn_image_get_class_count   image_get_class_count   = nullptr;
pfn_image_get_class         image_get_class         = nullptr;
pfn_class_from_name         class_from_name         = nullptr;
pfn_class_get_name          class_get_name          = nullptr;
pfn_class_get_namespace     class_get_namespace     = nullptr;
pfn_class_get_parent        class_get_parent        = nullptr;
pfn_class_get_methods       class_get_methods       = nullptr;
pfn_class_get_method_from_name class_get_method_from_name = nullptr;
pfn_class_get_fields        class_get_fields        = nullptr;
pfn_class_get_type          class_get_type          = nullptr;
pfn_class_is_valuetype      class_is_valuetype      = nullptr;
pfn_method_get_name         method_get_name         = nullptr;
pfn_method_get_param_count  method_get_param_count  = nullptr;
pfn_method_get_param        method_get_param         = nullptr;
pfn_method_get_return_type  method_get_return_type  = nullptr;
pfn_method_is_instance      method_is_instance      = nullptr;
pfn_field_get_name          field_get_name          = nullptr;
pfn_field_get_offset        field_get_offset        = nullptr;
pfn_field_get_type          field_get_type          = nullptr;
pfn_field_get_flags         field_get_flags         = nullptr;
pfn_class_get_properties    class_get_properties    = nullptr;
pfn_property_get_name       property_get_name       = nullptr;
pfn_property_get_get_method property_get_get_method = nullptr;
pfn_field_static_get_value  field_static_get_value  = nullptr;
pfn_field_static_set_value  field_static_set_value  = nullptr;
pfn_field_set_value         field_set_value         = nullptr;
pfn_field_get_value         field_get_value         = nullptr;
pfn_type_get_name           type_get_name           = nullptr;
pfn_type_get_object         type_get_object         = nullptr;
pfn_runtime_invoke          runtime_invoke          = nullptr;
pfn_object_new              object_new              = nullptr;
pfn_string_new              string_new              = nullptr;
pfn_thread_attach           thread_attach           = nullptr;
pfn_thread_detach           thread_detach           = nullptr;
pfn_gc_disable              gc_disable              = nullptr;
pfn_gc_enable               gc_enable               = nullptr;

static uintptr_t g_base = 0;
static bool      g_ready = false;

uintptr_t base() { return g_base; }
static bool ok()  { return g_ready; }

// ---------------------------------------------------------------------------
// 从已加载的 ELF 镜像查找库及其导出表。
// ---------------------------------------------------------------------------
struct LoadedImage {
    uintptr_t base = 0;
    const ElfW(Dyn)* dynamic = nullptr;
    std::string name;
};

static int find_image(dl_phdr_info* info, size_t, void* data) {
    const char* name = info->dlpi_name;
    if (!name) return 0;
    const char* last = strrchr(name, '/');
    if (strcmp(last ? last + 1 : name, "libil2cpp.so") != 0) return 0;
    auto* image = static_cast<LoadedImage*>(data);
    image->base = info->dlpi_addr;
    image->name = name;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            image->dynamic = reinterpret_cast<const ElfW(Dyn)*>(
                    image->base + info->dlpi_phdr[i].p_vaddr);
            return 1;
        }
    }
    return 0;
}

static uintptr_t dynamic_address(uintptr_t base, ElfW(Addr) value) {
    return value < base ? base + value : value;
}

static void* find_export(const LoadedImage& image, const char* name) {
    const ElfW(Sym)* symbols = nullptr;
    const char* strings = nullptr;
    const uint32_t* hash = nullptr;
    for (const ElfW(Dyn)* d = image.dynamic; d && d->d_tag != DT_NULL; ++d) {
        if (d->d_tag == DT_SYMTAB)
            symbols = reinterpret_cast<const ElfW(Sym)*>(dynamic_address(image.base, d->d_un.d_ptr));
        else if (d->d_tag == DT_STRTAB)
            strings = reinterpret_cast<const char*>(dynamic_address(image.base, d->d_un.d_ptr));
        else if (d->d_tag == DT_GNU_HASH)
            hash = reinterpret_cast<const uint32_t*>(dynamic_address(image.base, d->d_un.d_ptr));
    }
    if (!symbols || !strings || !hash || hash[0] == 0) return nullptr;

    uint32_t h = 5381;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
        h = h * 33 + *p;
    uint32_t bucketsCount = hash[0], symbolOffset = hash[1], bloomSize = hash[2];
    const ElfW(Addr)* bloom = reinterpret_cast<const ElfW(Addr)*>(hash + 4);
    const uint32_t* buckets = reinterpret_cast<const uint32_t*>(bloom + bloomSize);
    const uint32_t* chain = buckets + bucketsCount;
    uint32_t index = buckets[h % bucketsCount];
    if (index < symbolOffset) return nullptr;
    for (;;) {
        uint32_t value = chain[index - symbolOffset];
        if ((value | 1u) == (h | 1u) && symbols[index].st_shndx != SHN_UNDEF
                && strcmp(strings + symbols[index].st_name, name) == 0)
            return reinterpret_cast<void*>(image.base + symbols[index].st_value);
        if (value & 1u) return nullptr;
        ++index;
    }
}

bool init() {
    if (g_ready) return true;

    LoadedImage image;
    dl_iterate_phdr(find_image, &image);
    if (!image.dynamic) return false;
    void* h = dlopen(image.name.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!h) {
        const char* error = dlerror();
        LOGI("dlopen unavailable for %s: %s; using ELF exports", image.name.c_str(), error ? error : "unknown");
    }
    LOGI("libil2cpp.so loaded: %s base=0x%lx", image.name.c_str(), (unsigned long)image.base);

#define RESOLVE(field, sym)                                                    \
    field = (decltype(field))(h ? dlsym(h, sym) : nullptr);                    \
    if (!field) field = (decltype(field))find_export(image, sym);              \
    if (!field) LOGE("dlsym missing: %s", sym);

    RESOLVE(domain_get,              "il2cpp_domain_get")
    RESOLVE(domain_get_assemblies,   "il2cpp_domain_get_assemblies")
    RESOLVE(assembly_get_image,      "il2cpp_assembly_get_image")
    RESOLVE(image_get_name,          "il2cpp_image_get_name")
    RESOLVE(image_get_class_count,   "il2cpp_image_get_class_count")
    RESOLVE(image_get_class,         "il2cpp_image_get_class")
    RESOLVE(class_from_name,         "il2cpp_class_from_name")
    RESOLVE(class_get_name,          "il2cpp_class_get_name")
    RESOLVE(class_get_namespace,     "il2cpp_class_get_namespace")
    RESOLVE(class_get_parent,        "il2cpp_class_get_parent")
    RESOLVE(class_get_methods,       "il2cpp_class_get_methods")
    RESOLVE(class_get_method_from_name, "il2cpp_class_get_method_from_name")
    RESOLVE(class_get_fields,        "il2cpp_class_get_fields")
    RESOLVE(class_get_type,          "il2cpp_class_get_type")
    RESOLVE(class_is_valuetype,      "il2cpp_class_is_valuetype")
    RESOLVE(method_get_name,         "il2cpp_method_get_name")
    RESOLVE(method_get_param_count,  "il2cpp_method_get_param_count")
    RESOLVE(method_get_param,        "il2cpp_method_get_param")
    RESOLVE(method_get_return_type,  "il2cpp_method_get_return_type")
    RESOLVE(method_is_instance,      "il2cpp_method_is_instance")
    RESOLVE(field_get_name,          "il2cpp_field_get_name")
    RESOLVE(field_get_offset,        "il2cpp_field_get_offset")
    RESOLVE(field_get_type,          "il2cpp_field_get_type")
    RESOLVE(field_get_flags,         "il2cpp_field_get_flags")
    RESOLVE(class_get_properties,    "il2cpp_class_get_properties")
    RESOLVE(property_get_name,       "il2cpp_property_get_name")
    RESOLVE(property_get_get_method, "il2cpp_property_get_get_method")
    RESOLVE(field_static_get_value,  "il2cpp_field_static_get_value")
    RESOLVE(field_static_set_value,  "il2cpp_field_static_set_value")
    RESOLVE(field_set_value,         "il2cpp_field_set_value")
    RESOLVE(field_get_value,         "il2cpp_field_get_value")
    RESOLVE(type_get_name,           "il2cpp_type_get_name")
    RESOLVE(type_get_object,         "il2cpp_type_get_object")
    RESOLVE(runtime_invoke,          "il2cpp_runtime_invoke")
    RESOLVE(object_new,              "il2cpp_object_new")
    RESOLVE(string_new,              "il2cpp_string_new")
    RESOLVE(thread_attach,           "il2cpp_thread_attach")
    RESOLVE(thread_detach,           "il2cpp_thread_detach")
    RESOLVE(gc_disable,              "il2cpp_gc_disable")
    RESOLVE(gc_enable,               "il2cpp_gc_enable")
#undef RESOLVE

    if (!domain_get || !domain_get_assemblies || !assembly_get_image || !class_from_name
            || !class_get_methods || !class_get_method_from_name || !class_get_fields
            || !method_get_name || !method_get_param_count || !method_get_return_type
            || !field_get_name || !runtime_invoke || !thread_attach) {
        LOGE("core il2cpp symbols unavailable, abort");
        if (h) dlclose(h);
        return false;
    }
    g_base = image.base;
    g_ready = true;
    LOGI("il2cpp api ready");
    return true;
}

bool wait_and_init(int timeout_ms) {
    for (int i = 0; i < timeout_ms / 100; ++i) {
        if (init()) return true;
        if (i && i % 100 == 0) LOGI("still waiting for loaded libil2cpp.so (%d seconds)", i / 10);
        usleep(100 * 1000);
    }
    LOGE("wait_and_init timeout after %d ms", timeout_ms);
    return false;
}

// ---------------------------------------------------------------------------
// 查找
// ---------------------------------------------------------------------------
Il2CppClass* find_class(const char* ns, const char* name) {
    if (!ok() || !name) return nullptr;

    // 先查主程序集（最快路径，99% 命中）
    size_t n = 0;
    const Il2CppAssembly** as = domain_get_assemblies(domain_get(), &n);
    if (as) {
        for (size_t i = 0; i < n; ++i) {
            const Il2CppImage* img = assembly_get_image(as[i]);
            if (!img) continue;
            Il2CppClass* k = class_from_name(img, ns ? ns : "", name);
            if (k) return k;
        }
    }
    return nullptr;
}

Il2CppClass* find_class_any(const char* name) {
    if (!ok() || !name) return nullptr;
    size_t n = 0;
    const Il2CppAssembly** as = domain_get_assemblies(domain_get(), &n);
    if (!as) return nullptr;
    for (size_t i = 0; i < n; ++i) {
        const Il2CppImage* img = assembly_get_image(as[i]);
        if (!img) continue;
        size_t cc = image_get_class_count(img);
        for (size_t j = 0; j < cc; ++j) {
            Il2CppClass* k = image_get_class(img, j);
            if (!k) continue;
            const char* cn = class_get_name(k);
            if (cn && strcmp(cn, name) == 0) return k;
        }
    }
    return nullptr;
}

MethodInfo* find_method(Il2CppClass* k, const char* name, int argc) {
    if (!k || !name) return nullptr;
    if (argc >= 0 && class_get_method_from_name)
        return class_get_method_from_name(k, name, argc);

    void* iter = nullptr;
    const MethodInfo* m = nullptr;
    while ((m = class_get_methods(k, &iter)) != nullptr) {
        const char* mn = method_get_name(m);
        if (mn && strcmp(mn, name) == 0) return const_cast<MethodInfo*>(m);
    }
    return nullptr;
}

MethodInfo* find_method_prefix(Il2CppClass* k, const char* prefix) {
    if (!k || !prefix) return nullptr;
    void* iter = nullptr;
    const MethodInfo* m = nullptr;
    while ((m = class_get_methods(k, &iter)) != nullptr) {
        const char* mn = method_get_name(m);
        if (mn && strncmp(mn, prefix, strlen(prefix)) == 0)
            return const_cast<MethodInfo*>(m);
    }
    return nullptr;
}

FieldInfo* find_field(Il2CppClass* k, const char* name) {
    if (!k || !name) return nullptr;
    void* iter = nullptr;
    FieldInfo* f = nullptr;
    while ((f = class_get_fields(k, &iter)) != nullptr) {
        const char* fn = field_get_name(f);
        if (fn && strcmp(fn, name) == 0) return f;
    }
    return nullptr;
}

void* method_ptr(MethodInfo* m) {
    if (!m) return nullptr;
    // MethodInfo 首字段即 methodPointer（il2cpp 2019-2024 一致）
    return *(void**)m;
}

std::string full_name(Il2CppClass* k) {
    if (!k) return "<null>";
    const char* ns = class_get_namespace(k);
    const char* n  = class_get_name(k);
    std::string s;
    if (ns && *ns) { s = ns; s += "."; }
    s += (n ? n : "?");
    return s;
}

} // namespace ic

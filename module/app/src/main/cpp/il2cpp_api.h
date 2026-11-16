#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// ---------------------------------------------------------------------------
// 不透明类型：我们不 include il2cpp 头，也不依赖任何结构体布局，
// 全部通过 dlsym 出来的导出函数访问。唯一例外：MethodInfo 的
// methodPointer 在偏移 0（il2cpp 2019 至今未变），用于拿可 hook 的地址。
// ---------------------------------------------------------------------------
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppObject;
struct MethodInfo;
struct FieldInfo;
struct PropertyInfo;
struct Il2CppType;

namespace ic {

// ---- 函数指针类型 ----
using pfn_domain_get              = Il2CppDomain* (*)();
using pfn_domain_get_assemblies   = const Il2CppAssembly** (*)(const Il2CppDomain*, size_t*);
using pfn_assembly_get_image      = const Il2CppImage* (*)(const Il2CppAssembly*);
using pfn_image_get_name          = const char* (*)(const Il2CppImage*);
using pfn_image_get_class_count   = size_t (*)(const Il2CppImage*);
using pfn_image_get_class         = Il2CppClass* (*)(const Il2CppImage*, size_t);
using pfn_class_from_name         = Il2CppClass* (*)(const Il2CppImage*, const char*, const char*);
using pfn_class_get_name          = const char* (*)(Il2CppClass*);
using pfn_class_get_namespace     = const char* (*)(Il2CppClass*);
using pfn_class_get_parent        = Il2CppClass* (*)(Il2CppClass*);
using pfn_class_get_methods       = const MethodInfo* (*)(Il2CppClass*, void**);
using pfn_class_get_method_from_name = MethodInfo* (*)(Il2CppClass*, const char*, int);
using pfn_class_get_fields        = FieldInfo* (*)(Il2CppClass*, void**);
using pfn_class_get_type          = Il2CppType* (*)(Il2CppClass*);
using pfn_class_is_valuetype      = bool (*)(Il2CppClass*);
using pfn_method_get_name         = const char* (*)(const MethodInfo*);
using pfn_method_get_param_count  = uint32_t (*)(const MethodInfo*);
using pfn_method_get_param        = const Il2CppType* (*)(const MethodInfo*, uint32_t);
using pfn_method_get_return_type  = const Il2CppType* (*)(const MethodInfo*);
using pfn_method_is_instance      = bool (*)(const MethodInfo*);
using pfn_field_get_name          = const char* (*)(FieldInfo*);
using pfn_field_get_offset        = size_t (*)(FieldInfo*);
using pfn_field_get_type          = const Il2CppType* (*)(FieldInfo*);
using pfn_field_get_flags         = uint32_t (*)(FieldInfo*);
using pfn_class_get_properties    = const PropertyInfo* (*)(Il2CppClass*, void**);
using pfn_property_get_name       = const char* (*)(const PropertyInfo*);
using pfn_property_get_get_method = const MethodInfo* (*)(const PropertyInfo*);
using pfn_field_static_get_value  = void (*)(FieldInfo*, void*);
using pfn_field_static_set_value  = void (*)(FieldInfo*, void*);
using pfn_field_set_value         = void (*)(Il2CppObject*, FieldInfo*, void*);
using pfn_field_get_value         = void (*)(Il2CppObject*, FieldInfo*, void*);
using pfn_type_get_name           = char* (*)(const Il2CppType*);
using pfn_type_get_object         = Il2CppObject* (*)(const Il2CppType*);
using pfn_runtime_invoke          = Il2CppObject* (*)(MethodInfo*, void*, void**, Il2CppObject**);
using pfn_object_new              = Il2CppObject* (*)(Il2CppClass*);
using pfn_string_new              = Il2CppObject* (*)(const char*);
using pfn_thread_attach           = void* (*)(Il2CppDomain*);
using pfn_thread_detach           = void (*)(void*);
using pfn_gc_disable              = void (*)();
using pfn_gc_enable               = void (*)();

// ---- 实际符号 ----
extern pfn_domain_get              domain_get;
extern pfn_domain_get_assemblies   domain_get_assemblies;
extern pfn_assembly_get_image      assembly_get_image;
extern pfn_image_get_name          image_get_name;
extern pfn_image_get_class_count   image_get_class_count;
extern pfn_image_get_class         image_get_class;
extern pfn_class_from_name         class_from_name;
extern pfn_class_get_name          class_get_name;
extern pfn_class_get_namespace     class_get_namespace;
extern pfn_class_get_parent        class_get_parent;
extern pfn_class_get_methods       class_get_methods;
extern pfn_class_get_method_from_name class_get_method_from_name;
extern pfn_class_get_fields        class_get_fields;
extern pfn_class_get_type          class_get_type;
extern pfn_class_is_valuetype      class_is_valuetype;
extern pfn_method_get_name         method_get_name;
extern pfn_method_get_param_count  method_get_param_count;
extern pfn_method_get_param        method_get_param;
extern pfn_method_get_return_type  method_get_return_type;
extern pfn_method_is_instance      method_is_instance;
extern pfn_field_get_name          field_get_name;
extern pfn_field_get_offset        field_get_offset;
extern pfn_field_get_type          field_get_type;
extern pfn_field_get_flags         field_get_flags;
extern pfn_class_get_properties    class_get_properties;
extern pfn_property_get_name       property_get_name;
extern pfn_property_get_get_method property_get_get_method;
extern pfn_field_static_get_value  field_static_get_value;
extern pfn_field_static_set_value  field_static_set_value;
extern pfn_field_set_value         field_set_value;
extern pfn_field_get_value         field_get_value;
extern pfn_type_get_name           type_get_name;
extern pfn_type_get_object         type_get_object;
extern pfn_runtime_invoke          runtime_invoke;
extern pfn_object_new              object_new;
extern pfn_string_new              string_new;
extern pfn_thread_attach           thread_attach;
extern pfn_thread_detach           thread_detach;
extern pfn_gc_disable              gc_disable;
extern pfn_gc_enable               gc_enable;

// ---- 生命周期 ----
uintptr_t base();                     // libil2cpp.so 基址，0 = 未加载
bool      init();                     // dlopen + dlsym
bool      wait_and_init(int timeout_ms);

// ---- 便捷封装 ----
Il2CppClass* find_class(const char* ns, const char* name);
MethodInfo*  find_method(Il2CppClass* k, const char* name, int argc = -1);
MethodInfo*  find_method_prefix(Il2CppClass* k, const char* prefix);
FieldInfo*   find_field(Il2CppClass* k, const char* name);
void*        method_ptr(MethodInfo* m);   // MethodInfo->methodPointer
Il2CppClass* find_class_any(const char* name);   // 忽略 namespace

// 打印用
std::string full_name(Il2CppClass* k);

} // namespace ic

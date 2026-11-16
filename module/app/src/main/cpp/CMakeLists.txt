cmake_minimum_required(VERSION 3.22.1)
project(mobctrl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mobctrl SHARED
        entry.cpp
        il2cpp_api.cpp
        inline_hook.cpp
        mod_features.cpp
        dump.cpp
        log.cpp)

target_compile_options(mobctrl PRIVATE
        -O2
        -Wall
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-parameter)

# log   : __android_log_print
# dl    : dlopen / dlsym
# android: 预留（后续接 JNI 回调或系统属性时用）
target_link_libraries(mobctrl PRIVATE log dl android)

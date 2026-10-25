# God of War: Ascension (PS3) — Native x86-64 Static Recompilation
#
# Wires the lifted PPU code to ps3recomp's native runtime harness:
# loads EBOOT.ELF -> registers lifted functions + HLE NID handlers -> executes game code.
#
# Toolchain: Clang with UCRT (or clang-cl / MSVC)
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ./build/gow_ascension.exe ./vfs/PS3_GAME/USRDIR/EBOOT.ELF

cmake_minimum_required(VERSION 3.20)
project(gow_ascension CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT MSVC)
    add_compile_options(-fms-extensions)
endif()

set(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ps3recomp" CACHE PATH "ps3recomp repo root")

# Prefer prebuilt libps3recomp_runtime.a (MinGW UCRT) or ps3recomp_runtime.lib (MSVC)
if(EXISTS "${PS3RECOMP_DIR}/build/libps3recomp_runtime.a")
    set(PS3RECOMP_RUNTIME_LIB "${PS3RECOMP_DIR}/build/libps3recomp_runtime.a" CACHE FILEPATH "prebuilt runtime library")
else()
    set(PS3RECOMP_RUNTIME_LIB "${PS3RECOMP_DIR}/build/ps3recomp_runtime.lib" CACHE FILEPATH "prebuilt runtime library")
endif()

# Lifted game code (8 chunks + NID table)
file(GLOB RECOMP_SOURCES "src/recomp/*.cpp")
file(GLOB GEN_SOURCES    "src/gen/*.cpp" "src/gen/*.c")

# Lifted SPU images (Phase 3)
file(GLOB_RECURSE SPU_SOURCES "src/gen/spu/*.c" "src/spu_gen/*.c")

# LAW 4: Compile generated code in Release /O2 / -O2 without full debug PDBs to protect disk space on C:
if(MSVC)
    set_source_files_properties(${GEN_SOURCES} ${SPU_SOURCES} PROPERTIES COMPILE_FLAGS "/O2")
else()
    set_source_files_properties(${GEN_SOURCES} ${SPU_SOURCES} PROPERTIES COMPILE_FLAGS "-O2 -g0")
endif()

add_executable(gow_ascension
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/ascension_hle.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/psn_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/gow_inflate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/upstream_compat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/ascension_defaults.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    ${GEN_SOURCES}
    ${SPU_SOURCES}
    ${RECOMP_SOURCES}
)

file(GLOB LIB_INC_DIRS LIST_DIRECTORIES true "${PS3RECOMP_DIR}/libs/*")

target_include_directories(gow_ascension PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/spu"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/include"
    ${LIB_INC_DIRS}
)

target_link_libraries(gow_ascension PRIVATE "${PS3RECOMP_RUNTIME_LIB}")

if(WIN32)
    target_link_libraries(gow_ascension PRIVATE
        ws2_32 bcrypt xinput ole32 d3d12 dxgi dxguid d3dcompiler
        mfplat mfuuid winmm pdh)

    # 256 MB main thread stack reservation for deep PPC64 call nesting
    if(MSVC)
        target_link_options(gow_ascension PRIVATE /STACK:268435456 /MAP:gow_ascension.map)
    else()
        target_link_options(gow_ascension PRIVATE -static -Wl,--stack,268435456)
    endif()
endif()

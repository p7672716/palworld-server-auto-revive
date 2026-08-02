cmake_minimum_required(VERSION 3.22)
project(PalworldServerAutoReviveNative LANGUAGES CXX)

if(NOT DEFINED UE4SS_SRC OR NOT DEFINED UE4SS_LIB OR NOT DEFINED UE4SS_BUILD)
    message(FATAL_ERROR "Pass -DUE4SS_SRC=<ue4ss source> -DUE4SS_BUILD=<ue4ss build> and -DUE4SS_LIB=<libUE4SS.so>")
endif()

add_library(PalworldServerAutoReviveNative SHARED
    src/dllmain.cpp
)

file(GLOB UE4SS_FIRST_PARTY_INCLUDE_DIRS LIST_DIRECTORIES true
    "${UE4SS_SRC}/deps/first/*/include"
)

target_compile_features(PalworldServerAutoReviveNative PRIVATE cxx_std_23)
target_compile_options(PalworldServerAutoReviveNative PRIVATE -fno-omit-frame-pointer)
target_compile_definitions(PalworldServerAutoReviveNative PRIVATE
    UE4SS_ENABLE_IMGUI=0
    RC_UE4SS_API=
    FORCE_U16
    UE4SS_LIB_VERSION_MAJOR=3
    UE4SS_LIB_VERSION_MINOR=0
    UE4SS_LIB_VERSION_HOTFIX=1
    UE4SS_LIB_VERSION_PRERELEASE=0
    UE4SS_LIB_VERSION_BETA=1
    UE4SS_LIB_BETA_STARTED=1
    UE4SS_LIB_IS_BETA=1
    UE4SS_CONFIGURATION="Game__Dev__Linux64"
    OSTYPE=Linux
    PLATFORM_LINUX=1
    UBT_COMPILED_PLATFORM=Linux
    STATS
    UE_BUILD_DEVELOPMENT=1
    UE_GAME=1
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(PalworldServerAutoReviveNative PRIVATE
    "${UE4SS_SRC}/UE4SS/include"
    "${UE4SS_SRC}/deps/first/Unreal/include"
    "${UE4SS_SRC}/deps/first/Unreal/include/Unreal/Core"
    "${UE4SS_SRC}/deps/first/Unreal/include/Unreal"
    "${UE4SS_SRC}/deps/first/Unreal/generated_include"
    "${UE4SS_SRC}/deps/third/fmt/include"
    "${UE4SS_BUILD}/_deps/fmt-src/include"
    "${UE4SS_BUILD}/_deps/zydis-src/include"
    "${UE4SS_BUILD}/_deps/zydis-src/dependencies/zycore/include"
    "${UE4SS_SRC}/deps/first/Helpers/include"
    "${UE4SS_SRC}/deps/first/ASMHelper/include"
    "${UE4SS_SRC}/deps/first/SinglePassSigScanner/include"
    ${UE4SS_FIRST_PARTY_INCLUDE_DIRS}
)

target_link_libraries(PalworldServerAutoReviveNative PRIVATE "${UE4SS_LIB}")

set_target_properties(PalworldServerAutoReviveNative PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)



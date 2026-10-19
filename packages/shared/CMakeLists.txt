cmake_minimum_required(VERSION 3.25)
project(SpacewingShared VERSION 0.1.0 LANGUAGES CXX)

# Portable value types and header-only utilities. Native consumers opt into SharedMacOS.
add_library(spacewing_shared INTERFACE)
add_library(Spacewing::Shared ALIAS spacewing_shared)
target_include_directories(spacewing_shared INTERFACE src)
target_compile_features(spacewing_shared INTERFACE cxx_std_20)

if(APPLE)
    enable_language(OBJCXX)
    add_library(spacewing_shared_macos STATIC src/macos.mm)
    add_library(Spacewing::SharedMacOS ALIAS spacewing_shared_macos)
    set_target_properties(spacewing_shared_macos PROPERTIES
        OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES OBJCXX_EXTENSIONS NO)
    target_compile_options(spacewing_shared_macos PRIVATE -fobjc-arc -Wall -Wextra -Wpedantic)
    target_link_libraries(spacewing_shared_macos PUBLIC Spacewing::Shared
        "-framework AppKit" "-framework ApplicationServices")
endif()

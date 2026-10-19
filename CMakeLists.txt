cmake_minimum_required(VERSION 3.25)
project(Spacewing VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT APPLE)
    message(FATAL_ERROR "Spacewing requires macOS")
endif()

set(SPACEWING_CODESIGN_IDENTITY "-" CACHE STRING
    "Code signing certificate name or SHA-1 fingerprint; - means ad-hoc signing")
option(SPACEWING_DISTRIBUTION
    "Require Developer ID signing, hardened runtime, and secure timestamps" OFF)
if(SPACEWING_CODESIGN_IDENTITY STREQUAL "")
    message(FATAL_ERROR "SPACEWING_CODESIGN_IDENTITY must be a certificate name, fingerprint, or -")
endif()
if(SPACEWING_DISTRIBUTION AND SPACEWING_CODESIGN_IDENTITY STREQUAL "-")
    message(FATAL_ERROR "Distribution builds require a Developer ID Application signing identity")
endif()
set(spacewing_distribution_mode OFF)
if(SPACEWING_DISTRIBUTION)
    set(spacewing_distribution_mode ON)
endif()
if(SPACEWING_CODESIGN_IDENTITY STREQUAL "-")
    message(STATUS "Ad-hoc signing: Accessibility grants may need renewing after rebuilds. Set SPACEWING_CODESIGN_IDENTITY for persistent identity.")
endif()
# Make packaging rerun when signing settings change, even without recompilation.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/signing-config.txt"
    CONTENT "identity=${SPACEWING_CODESIGN_IDENTITY}\ndistribution=${spacewing_distribution_mode}\n")

include(CTest)
add_subdirectory(packages/shared)
add_subdirectory(packages/workspace)
add_subdirectory(packages/switcher)

# The host owns the combined app's CLI, config, IPC, menu and Settings plumbing.
# Feature packages never include src/ or depend on this target.
add_library(spacewing_host STATIC
    src/command.cc src/executor.cc src/config/config.cc
    src/ipc/socket.cc src/platform/application.mm)
target_include_directories(spacewing_host PUBLIC src)
target_include_directories(spacewing_host SYSTEM PRIVATE third_party/toml11)
target_compile_options(spacewing_host PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(spacewing_host PUBLIC Spacewing::WorkspaceModel
    Spacewing::SwitcherModel Spacewing::SharedMacOS)

add_executable(spacewing src/main.cc src/runtime.cc)
target_link_libraries(spacewing PRIVATE spacewing_host Spacewing::Workspace Spacewing::Switcher)
target_compile_options(spacewing PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(spacewing PROPERTIES
    OUTPUT_NAME Spacewing RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(spacewing-settings src/platform/settings.mm)
target_compile_options(spacewing-settings PRIVATE -fobjc-arc -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(spacewing-settings PRIVATE spacewing_host)
set_target_properties(spacewing-settings PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Publish a completely signed copy; the compiler's outputs are never the running app.
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/.bundle-stamp"
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package.sh"
        "${CMAKE_BINARY_DIR}" "$<TARGET_FILE:spacewing>" "$<TARGET_FILE:spacewing-settings>" "${CMAKE_CURRENT_SOURCE_DIR}"
        "${SPACEWING_CODESIGN_IDENTITY}" "${spacewing_distribution_mode}"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "Spacewing.app/Contents/MacOS/Spacewing" "${CMAKE_BINARY_DIR}/spacewing"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/.bundle-stamp"
    DEPENDS spacewing spacewing-settings scripts/package.sh resources/Info.plist.in
        resources/SettingsInfo.plist resources/default.toml THIRD_PARTY_NOTICES.md
        "${CMAKE_BINARY_DIR}/signing-config.txt"
    VERBATIM COMMENT "Package and sign Spacewing.app")
add_custom_target(bundle ALL DEPENDS "${CMAKE_BINARY_DIR}/.bundle-stamp")

if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cc)
    target_link_libraries(core_tests PRIVATE spacewing_host)
    add_test(NAME core COMMAND core_tests)
    add_test(NAME ipc_integration COMMAND core_tests --ipc)
    # Restricted runners can prohibit local Unix-socket binds; other errors remain failures.
    set_tests_properties(ipc_integration PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME cli_help COMMAND spacewing --help)
    add_test(NAME cli_version COMMAND spacewing --version)
endif()

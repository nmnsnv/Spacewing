cmake_minimum_required(VERSION 3.25)
project(SpacewingWorkspace VERSION 0.1.0 LANGUAGES CXX)

# Each feature can be configured on its own with shared as its only sibling dependency.
if(NOT TARGET Spacewing::Shared)
    add_subdirectory(../shared "${CMAKE_CURRENT_BINARY_DIR}/shared")
endif()

add_library(spacewing_workspace_model STATIC src/shortcuts.cc)
add_library(Spacewing::WorkspaceModel ALIAS spacewing_workspace_model)
target_include_directories(spacewing_workspace_model PUBLIC src)
target_compile_features(spacewing_workspace_model PUBLIC cxx_std_20)
target_compile_options(spacewing_workspace_model PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(spacewing_workspace_model PUBLIC Spacewing::Shared)

if(APPLE)
    enable_language(OBJCXX)
    add_library(spacewing_workspace STATIC
        src/controller.cc
        src/gesture.cc
        src/native.mm
        src/hotkeys.mm
        src/window_move.mm)
    add_library(Spacewing::Workspace ALIAS spacewing_workspace)
    set_target_properties(spacewing_workspace PROPERTIES
        OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES OBJCXX_EXTENSIONS NO)
    target_compile_options(spacewing_workspace PRIVATE -Wall -Wextra -Wpedantic
        -Wno-unused-parameter "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(spacewing_workspace PUBLIC Spacewing::WorkspaceModel
        Spacewing::SharedMacOS "-framework Carbon")
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
if(BUILD_TESTING)
    add_executable(workspace_shortcut_tests tests/shortcuts_tests.cc)
    target_link_libraries(workspace_shortcut_tests PRIVATE Spacewing::WorkspaceModel)
    add_test(NAME workspace_shortcuts COMMAND workspace_shortcut_tests)
    if(APPLE)
        add_executable(workspace_tests tests/workspace_tests.cc)
        target_link_libraries(workspace_tests PRIVATE Spacewing::Workspace)
        add_test(NAME workspace COMMAND workspace_tests)
    endif()
endif()

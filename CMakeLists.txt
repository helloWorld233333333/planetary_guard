cmake_minimum_required(VERSION 3.15)

project(PlanetaryGuard VERSION 0.1.7 LANGUAGES CXX)

if(MSVC AND MSVC_VERSION LESS 1929)
    message(FATAL_ERROR
        "Planetary Guard requires MSVC 19.29 or newer for the C++20 build. "
        "Please update Visual Studio and reconfigure the build directory.")
endif()

option(PLANETARY_GUARD_BUILD_TESTS "Build Planetary Guard unit tests" ON)
option(PLANETARY_GUARD_BUILD_APP "Build the native Windows dock application" ON)

# 项目规范固定使用 C++20。旧版 VS2017 会被上面的版本检查显式拒绝，
# 禁止在不支持 C++20 的工具链上静默降级。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    # 安装包必须能在未安装 Visual C++ Redistributable 的干净 Windows
    # 10/11 上运行，因此静态链接与当前配置匹配的 MSVC 运行库。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(planetary_core STATIC
    src/config/Json.cpp
    src/dock/AutoHideController.cpp
    src/layout/MagnificationModel.cpp
    src/layout/LayoutEngine.cpp
)

target_include_directories(planetary_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32 AND PLANETARY_GUARD_BUILD_APP AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    add_executable(planetary_guard WIN32
        src/main.cpp
        src/config/DockItemStore.cpp
        src/config/SettingsStore.cpp
        src/platform/DockWindow.cpp
        src/platform/DockRenderer.cpp
        src/platform/FullscreenDetector.cpp
        src/platform/IconLoader.cpp
        src/platform/IconNormalizer.cpp
        src/platform/ShellLauncher.cpp
        src/platform/WindowCatalog.cpp
        src/platform/SingleInstance.cpp
        src/platform/StartupManager.cpp
        src/platform/SettingsWindow.cpp
        src/platform/TrayController.cpp
        resources/planetary_guard.rc
    )

    target_link_libraries(planetary_guard PRIVATE
        planetary_core
        d2d1
        dwrite
        dwmapi
        shell32
        comctl32
        shlwapi
        comdlg32
        advapi32
        user32
        gdi32
        ole32
        oleaut32
        uuid
    )

    target_compile_definitions(planetary_guard PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # 使用 resources/planetary_guard.manifest 的 PerMonitorV2 声明，避免
    # Visual Studio 自动生成的默认 manifest 与自定义资源重复。
    set_target_properties(planetary_guard PROPERTIES LINK_FLAGS "/MANIFEST:NO")
    set_source_files_properties(resources/planetary_guard.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/planetary_guard.manifest")
    configure_file(packaging/setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)
    add_executable(planetary_setup WIN32 packaging/setup.cpp "${CMAKE_CURRENT_BINARY_DIR}/setup.rc")
    add_dependencies(planetary_setup planetary_guard)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/setup.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/planetary_guard.exe;${CMAKE_CURRENT_SOURCE_DIR}/resources/planetary_guard.manifest;${CMAKE_CURRENT_SOURCE_DIR}/README.md;${CMAKE_CURRENT_SOURCE_DIR}/packaging/uninstall.ps1")
    target_compile_definitions(planetary_setup PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(planetary_setup PRIVATE shell32 ole32 uuid advapi32 user32)
    set_target_properties(planetary_setup PROPERTIES LINK_FLAGS "/MANIFEST:NO")
endif()

if(PLANETARY_GUARD_BUILD_TESTS)
    enable_testing()

    add_executable(planetary_core_tests
        tests/layout_tests.cpp
    )
    target_link_libraries(planetary_core_tests PRIVATE planetary_core)
    add_test(NAME planetary_core_tests COMMAND planetary_core_tests)

    add_executable(planetary_config_tests
        tests/config_tests.cpp
    )
    target_link_libraries(planetary_config_tests PRIVATE planetary_core)
    add_test(NAME planetary_config_tests COMMAND planetary_config_tests)

    add_executable(planetary_auto_hide_tests
        tests/auto_hide_tests.cpp
    )
    target_link_libraries(planetary_auto_hide_tests PRIVATE planetary_core)
    add_test(NAME planetary_auto_hide_tests COMMAND planetary_auto_hide_tests)

    add_executable(planetary_icon_normalizer_tests
        tests/icon_normalizer_tests.cpp
        src/platform/IconNormalizer.cpp
    )
    target_link_libraries(planetary_icon_normalizer_tests PRIVATE planetary_core)
    add_test(NAME planetary_icon_normalizer_tests COMMAND planetary_icon_normalizer_tests)

    if(WIN32)
        if(TARGET planetary_guard)
            # 复用生产窗口实现，覆盖状态机到真实 HWND 可见性的边界。
            get_target_property(dock_test_sources planetary_guard SOURCES)
            list(REMOVE_ITEM dock_test_sources src/main.cpp resources/planetary_guard.rc)
            get_target_property(dock_test_libraries planetary_guard LINK_LIBRARIES)
            add_executable(planetary_dock_visibility_tests tests/dock_visibility_tests.cpp ${dock_test_sources})
            target_compile_definitions(planetary_dock_visibility_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
            target_link_libraries(planetary_dock_visibility_tests PRIVATE ${dock_test_libraries})
            add_test(NAME planetary_dock_visibility_tests COMMAND planetary_dock_visibility_tests)
        endif()
        add_executable(planetary_render_tests tests/render_tests.cpp src/platform/DockRenderer.cpp)
        target_compile_definitions(planetary_render_tests PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(planetary_render_tests PRIVATE planetary_core d2d1 dwrite dwmapi advapi32 gdi32 user32 ole32)
        add_test(NAME planetary_render_tests COMMAND planetary_render_tests)
        add_executable(planetary_render_benchmark tests/render_benchmark.cpp src/platform/DockRenderer.cpp)
        target_compile_definitions(planetary_render_benchmark PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(planetary_render_benchmark PRIVATE planetary_core d2d1 dwrite dwmapi advapi32 gdi32 user32 ole32)
        add_executable(planetary_window_tests tests/window_catalog_tests.cpp src/platform/WindowCatalog.cpp)
        target_include_directories(planetary_window_tests PRIVATE src)
        target_compile_definitions(planetary_window_tests PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(planetary_window_tests PRIVATE dwmapi shell32 ole32 uuid user32)
        add_test(NAME planetary_window_tests COMMAND planetary_window_tests)
        add_executable(planetary_settings_tests
            tests/settings_tests.cpp
            src/config/SettingsStore.cpp
        )
        target_link_libraries(planetary_settings_tests PRIVATE
            planetary_core
            shell32
            ole32
        )
        add_test(NAME planetary_settings_tests COMMAND planetary_settings_tests)

        add_executable(planetary_store_tests
            tests/store_tests.cpp
            src/config/DockItemStore.cpp
        )
        target_link_libraries(planetary_store_tests PRIVATE
            planetary_core
            shell32
            ole32
            uuid
        )
        add_test(NAME planetary_store_tests COMMAND planetary_store_tests)
    endif()
endif()

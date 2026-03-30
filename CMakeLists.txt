cmake_minimum_required(VERSION 3.14)
project(PackageManagerUiPlugin LANGUAGES CXX)

set(CMAKE_AUTORCC ON)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME package_manager_ui
    SOURCES
        src/package_manager_ui_plugin.h
        src/package_manager_ui_plugin.cpp
        src/PackageManagerBackend.h
        src/PackageManagerBackend.cpp
        src/PackageListModel.h
        src/PackageListModel.cpp
        src/PackageTypes.h
        src/PackageTypes.cpp
        src/package_manager_ui_resources.qrc
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
)

# Qt Widgets, Quick, and QuickWidgets are required for QML-based UI but not
# included by logos_module() — find and link them explicitly.
find_package(Qt6 REQUIRED COMPONENTS Widgets RemoteObjects Quick QuickWidgets QuickControls2)
target_link_libraries(package_manager_ui_module_plugin PRIVATE
    Qt6::Widgets
    Qt6::RemoteObjects
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::QuickControls2
)

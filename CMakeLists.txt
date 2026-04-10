cmake_minimum_required(VERSION 3.14)
project(PackageManagerUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME package_manager_ui
    REP_FILE src/package_manager_ui.rep
    SOURCES
        src/package_manager_ui_interface.h
        src/package_manager_ui_plugin.h
        src/package_manager_ui_plugin.cpp
        src/PackageManagerBackend.h
        src/PackageManagerBackend.cpp
        src/PackageListModel.h
        src/PackageListModel.cpp
        src/PackageTypes.h
        src/PackageTypes.cpp
)

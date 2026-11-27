cmake_minimum_required(VERSION 3.14)
project(PackageManagerUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# The name comes from metadata.json: the backend is built from it as the core
# module `package_manager_ui_backend`.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

# basecamp-installer-common, staged by the flake's preConfigure.
include(${CMAKE_CURRENT_SOURCE_DIR}/vendor/basecamp-installer-common/cmake/BasecampInstallerCommon.cmake)

logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/package_manager_ui.rep
    SOURCES
        src/PackageManagerBackend.h
        src/PackageManagerBackend.cpp
        src/Packages.h
        src/Packages.cpp
        src/ModuleCalls.h
        src/ModuleCalls.cpp
        ${BASECAMP_INSTALLER_COMMON_SOURCES}
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # <logos/semver.hpp>, staged from logos-package; headers only.
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS}
)

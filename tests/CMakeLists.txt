cmake_minimum_required(VERSION 3.14)
project(PackageManagerUiTests LANGUAGES CXX)

include(LogosTest)

logos_test(
    NAME package_manager_ui_tests
    MODULE_SOURCES
        ../src/PackageRowBuilder.cpp
        ../src/PackageTypes.cpp
        ../src/InstallSequence.cpp
    TEST_SOURCES
        main.cpp
        test_package_rows.cpp
        test_install_sequence.cpp
    EXTRA_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        # Shared semver headers, staged by the flake's preConfigure —
        # RowActionResolver.h includes <logos/semver.hpp>.
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor
)

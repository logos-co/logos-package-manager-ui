cmake_minimum_required(VERSION 3.16)
project(PackageManagerUiTests LANGUAGES CXX)

# The page's logic (src/Packages) on the library, with fake package modules
# and a fake Basecamp: no SDK, no host. And a read of the module's own sources.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(BASECAMP_INSTALLER_COMMON_DIR "" CACHE PATH "basecamp-installer-common's source")
set(LOGOS_PACKAGE_INCLUDE_DIR "" CACHE PATH "logos-package's headers (<logos/semver.hpp>)")

find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

include(${BASECAMP_INSTALLER_COMMON_DIR}/cmake/BasecampInstallerCommon.cmake)

set(PMUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_library(pmui_core STATIC
    ${BASECAMP_INSTALLER_COMMON_SOURCES}
    ${PMUI_SRC}/Packages.h
    ${PMUI_SRC}/Packages.cpp)
target_include_directories(pmui_core
    PUBLIC ${PMUI_SRC} ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS}
    PRIVATE ${LOGOS_PACKAGE_INCLUDE_DIR})
target_link_libraries(pmui_core PUBLIC Qt6::Core Qt6::Network)
target_compile_options(pmui_core PRIVATE -Wall -Wextra -Werror)

enable_testing()
foreach(t packages module_calls)
    add_executable(tst_${t} cpp/tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE pmui_core Qt6::Test)
    target_compile_options(tst_${t} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()

# tst_module_calls reads the module's own sources.
target_compile_definitions(tst_module_calls PRIVATE PMUI_SOURCE_DIR="${PMUI_SRC}")

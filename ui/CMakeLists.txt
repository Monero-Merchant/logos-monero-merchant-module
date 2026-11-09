cmake_minimum_required(VERSION 3.14)
project(MoneroMerchantUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Derive the module name from metadata.json — single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)

# Universal UI module: you write the .rep + the *Backend class. REP_FILE runs
# repc; the generated *Plugin glue in generated_code/ is compiled automatically.
logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/monero_merchant_ui.rep
    SOURCES
        src/monero_merchant_ui_backend.h
        src/monero_merchant_ui_backend.cpp
        src/payment_qr.cpp
    INCLUDE_DIRS
        src
)
target_link_libraries(${MODULE_NAME}_module_plugin PRIVATE PkgConfig::QRENCODE)

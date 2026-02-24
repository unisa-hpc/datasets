cmake_minimum_required(VERSION 3.16)

project(unisahpc_datasets VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(unisahpc_datasets INTERFACE)
add_library(unisahpc::datasets ALIAS unisahpc_datasets)
set_target_properties(unisahpc_datasets PROPERTIES EXPORT_NAME datasets)

target_compile_features(unisahpc_datasets INTERFACE cxx_std_17)

target_include_directories(unisahpc_datasets
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS unisahpc_datasets
  EXPORT unisahpc_datasetsTargets
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT unisahpc_datasetsTargets
  FILE unisahpc_datasetsTargets.cmake
  NAMESPACE unisahpc::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/unisahpc_datasets
)

configure_package_config_file(
  cmake/unisahpc_datasetsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/unisahpc_datasetsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/unisahpc_datasets
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/unisahpc_datasetsConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/unisahpc_datasetsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/unisahpc_datasetsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/unisahpc_datasets
)

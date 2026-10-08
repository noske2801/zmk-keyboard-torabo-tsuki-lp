# The pinned upstream ADC/low-voltage driver is retained verbatim except that
# its unsafe advertising timeout is replaced by src/disconnected_sleep.c.
# Generate into the build tree; never modify the west dependency checkout.
set(non_lipo_dir "${ZEPHYR_ZMK_FEATURE_NON_LIPO_BATTERY_MANAGEMENT_MODULE_DIR}")
set(non_lipo_source "${non_lipo_dir}/src/non_lipo_battery_management.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${non_lipo_source}")
file(SHA256 "${non_lipo_source}" non_lipo_sha256)
if(NOT non_lipo_sha256 STREQUAL "a5b1c474d9c9828d06e83d784599dcdb8f08efa5c97f90601b6053a16af7b0cc")
  message(FATAL_ERROR "Non-LiPo source changed: review the local timeout override before upgrading")
endif()
file(READ "${non_lipo_source}" non_lipo_contents)
string(REPLACE "#if IS_ENABLED(CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT)"
               "#if 0 /* Replaced by the local disconnected-sleep monitor. */"
               non_lipo_contents "${non_lipo_contents}")
set(non_lipo_generated "${CMAKE_CURRENT_BINARY_DIR}/non_lipo_battery_management.c")
file(WRITE "${non_lipo_generated}" "${non_lipo_contents}")

# module.yml's build.depends guarantees the upstream target already exists.
# Locate it by source, rather than depending on Zephyr's path-derived name.
get_property(non_lipo_targets DIRECTORY "${non_lipo_dir}" PROPERTY BUILDSYSTEM_TARGETS)
set(non_lipo_replaced 0)
foreach(non_lipo_target IN LISTS non_lipo_targets)
  get_target_property(non_lipo_sources "${non_lipo_target}" SOURCES)
  if(non_lipo_sources STREQUAL "src/non_lipo_battery_management.c")
    set_property(TARGET "${non_lipo_target}" PROPERTY SOURCES "${non_lipo_generated}")
    math(EXPR non_lipo_replaced "${non_lipo_replaced} + 1")
  endif()
endforeach()
if(NOT non_lipo_replaced EQUAL 1)
  message(FATAL_ERROR "Expected exactly one pinned Non-LiPo driver target")
endif()

# SPDX-License-Identifier: Apache-2.0

# Out-of-tree module glue. Upstream, the driver is added through
# drivers/sensor/CMakeLists.txt instead.
zephyr_include_directories(include)

if(CONFIG_SENSOR)
  add_subdirectory(drivers/sensor/hilink)
endif()

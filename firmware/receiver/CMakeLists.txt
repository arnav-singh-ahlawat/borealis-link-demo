cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(borealis_receiver)

include(../common/app.cmake)
target_sources(app PRIVATE
  main.c serial_zephyr.c
  "${DEMO_ROOT}/protocol/assemble.c"
  "${DEMO_ROOT}/protocol/ssdv_fec.c"
  "${DEMO_ROOT}/protocol/telemetry.c"
)

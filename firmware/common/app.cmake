set(DEMO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

target_include_directories(app PRIVATE
  "${DEMO_ROOT}/protocol/include"
  "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_sources(app PRIVATE
  "${DEMO_ROOT}/protocol/wire.c"
  "${CMAKE_CURRENT_LIST_DIR}/radio_zephyr.c"
)

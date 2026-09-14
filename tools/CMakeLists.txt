file(GLOB BT_FILES *.bt)
install(PROGRAMS ${BT_FILES} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/bpftrace/tools)

add_executable(ubpftrace-cat ubpftrace_cat.cpp)
target_include_directories(ubpftrace-cat PRIVATE ${CMAKE_SOURCE_DIR}/bpftime/runtime/include)
target_link_libraries(ubpftrace-cat PRIVATE -llz4)

add_executable(ubpftrace-top ubpftrace_top.cpp)
target_include_directories(ubpftrace-top PRIVATE
  ${CMAKE_SOURCE_DIR}/bpftime/runtime/include
  ${CMAKE_SOURCE_DIR}/bpftime/third_party
)
target_link_libraries(ubpftrace-top PRIVATE pthread)



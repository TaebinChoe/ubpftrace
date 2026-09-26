file(GLOB BT_FILES *.bt)
install(PROGRAMS ${BT_FILES} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/bpftrace/tools)

add_executable(ubt-cat ubpftrace_cat.cpp)
target_include_directories(ubt-cat PRIVATE ${CMAKE_SOURCE_DIR}/bpftime/runtime/include)
target_link_libraries(ubt-cat PRIVATE -llz4)

add_executable(ubt-top ubpftrace_top.cpp)
target_include_directories(ubt-top PRIVATE
  ${CMAKE_SOURCE_DIR}/bpftime/runtime/include
  ${CMAKE_SOURCE_DIR}/bpftime/third_party
)
target_link_libraries(ubt-top PRIVATE pthread)

add_executable(ubt-attach ubpftrace_attach.cpp)
target_link_libraries(ubt-attach PRIVATE stdc++fs)

# Compatibility aliases and bin directory synchronization
add_custom_target(ubpftrace-cat ALL DEPENDS ubt-cat)
add_custom_target(ubpftrace-top ALL DEPENDS ubt-top)
add_custom_target(ubpftrace-attach ALL DEPENDS ubt-attach)

add_custom_command(TARGET ubt-cat POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ubt-cat> ${CMAKE_SOURCE_DIR}/bin/ubt-cat
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-cat ${CMAKE_SOURCE_DIR}/bin/upt-cat
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-cat ${CMAKE_SOURCE_DIR}/bin/ubpftrace-cat
)

add_custom_command(TARGET ubt-top POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ubt-top> ${CMAKE_SOURCE_DIR}/bin/ubt-top
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-top ${CMAKE_SOURCE_DIR}/bin/upt-top
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-top ${CMAKE_SOURCE_DIR}/bin/ubpftrace-top
)

add_custom_command(TARGET ubt-attach POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ubt-attach> ${CMAKE_SOURCE_DIR}/bin/ubt-attach
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-attach ${CMAKE_SOURCE_DIR}/bin/upt-attach
  COMMAND ${CMAKE_COMMAND} -E create_symlink ubt-attach ${CMAKE_SOURCE_DIR}/bin/ubpftrace-attach
)







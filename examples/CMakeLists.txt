add_custom_target(all_examples)

function(add_example folder name)
  add_executable(${name} ${folder}/${name}.cpp ${ARGN})
  set_property(TARGET ${name} PROPERTY CXX_STANDARD 17)
  install(FILES ${folder}/${name}.cpp DESTINATION ${CMAKE_INSTALL_DATADIR}/caf/examples/${folder})
  add_dependencies(${name} all_examples)
endfunction()

# -- examples for CAF::net -----------------------------------------------------

if(TARGET CAF::net)
  function(add_net_example name)
    add_example("net" ${name} ${ARGN})
    target_link_libraries(${name} CAF::net CAF::core)
  endfunction()
  add_net_example(web-socket-calculator)
  add_net_example(web-socket-feed)
endif()

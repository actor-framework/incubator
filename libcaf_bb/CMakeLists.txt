cmake_minimum_required(VERSION 2.8.12)
project(caf_bb C CXX)

# get header files; only needed by CMake generators,
# e.g., for creating proper Xcode projects
file(GLOB_RECURSE LIBCAF_BB_HDRS "caf/*.hpp")

# --> Uncomment this block when adding the first .cpp file
#
# # list cpp files excluding platform-dependent files
# set(LIBCAF_BB_SRCS
# )
#
# add_custom_target(libcaf_bb)
#
# # build shared library if not compiling static only
# if (NOT CAF_BUILD_STATIC_ONLY)
#   add_library(libcaf_bb_shared SHARED ${LIBCAF_BB_SRCS} ${LIBCAF_BB_HDRS})
#   target_link_libraries(libcaf_bb_shared ${CAF_EXTRA_LDFLAGS})
#   target_include_directories(libcaf_bb_shared PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
#     $<INSTALL_INTERFACE:include>
#   )
#   set_target_properties(libcaf_bb_shared
#     PROPERTIES
#     SOVERSION ${CAF_VERSION}
#     VERSION ${CAF_VERSION}
#     OUTPUT_NAME caf_bb
#   )
#   install(TARGETS libcaf_bb_shared
#     RUNTIME DESTINATION bin
#     LIBRARY DESTINATION lib
#   )
#   add_dependencies(libcaf_bb_shared libcaf_bb)
# endif ()
#
# # build static library only if --build-static or --build-static-only was set
# if (CAF_BUILD_STATIC_ONLY OR CAF_BUILD_STATIC)
#   add_library(libcaf_bb_static STATIC ${LIBCAF_BB_HDRS} ${LIBCAF_BB_SRCS})
#   target_link_libraries(libcaf_bb_static ${CAF_EXTRA_LDFLAGS})
#   target_include_directories(libcaf_bb_static PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
#     $<INSTALL_INTERFACE:include>
#   )
#   set_target_properties(libcaf_bb_static PROPERTIES OUTPUT_NAME caf_bb_static)
#   install(TARGETS libcaf_bb_static ARCHIVE DESTINATION lib)
#   add_dependencies(libcaf_bb_static libcaf_bb)
# endif ()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/caf"
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp")

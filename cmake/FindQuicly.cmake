# Try to find quicly headers and libraries
#
# Use this module as follows:
#
#     find_package(Quicly)
#
# Variables used by this module (they can change the default behavior and need
# to be set before calling find_package):
#
#  QUICLY_LIB_ROOT_DIR
#  QUICLY_ROOT_DIR Set this variable either to an installation prefix or to
#                   the QUICLY root directory where to look for the library.
#
# Variables defined by this module:
#
#  QUICLY_FOUND        Found library and header
#  QUICLY_LIBRARIES    Path to library
#  QUICLY_INCLUDE_DIRS Include path for headers
#

find_library(QUICLY_LIBRARIES
  NAMES quicly
  HINTS ${QUICLY_ROOT_DIR}/build)

find_path(QUICLY_INCLUDE_DIRS
  NAMES "quicly.h"
  HINTS ${QUICLY_ROOT_DIR}/include)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  QUICLY
  DEFAULT_MSG
  QUICLY_LIBRARIES
  QUICLY_INCLUDE_DIRS)

mark_as_advanced(
  QUICLY_ROOT_DIR
  QUICLY_LIBRARIES
  QUICLY_INCLUDE_DIRS)

# Try to find PicoTLS headers and libraries
#
# Use this module as follows:
#
#     find_package(PicoTLS)
#
# Variables used by this module (they can change the default behavior and need
# to be set before calling find_package):
#
#  PICOTLS_LIB_ROOT_DIR
#  PICOTLS_ROOT_DIR Set this variable either to an installation prefix or to
#                   the PICOTLS root directory where to look for the library.
#
# Variables defined by this module:
#
#  PICOTLS_FOUND        Found library and header
#  PICOTLS_LIBRARIES    Path to library
#  PICOTLS_INCLUDE_DIRS Include path for headers
#

find_path(PICOTLS_INCLUDE_DIR
    names "picotls.h"
    HINTS ${PICOTLS_ROOT_DIR}/include)

find_path(MINCRYPTO_INCLUDE_DIR
    NAMES "minicrypto.h"
    HINTS ${PICOTLS_ROOT_DIR}/include/picotls)

find_library(PICOTLS_CORE_LIBRARY
    NAMES picotls-core
    HINTS ${PICOTLS_ROOT_DIR}/build)

find_library(PICOTLS_MINICRYPTO_LIBRARY
    NAMES picotls-minicrypto
    HINTS ${PICOTLS_ROOT_DIR}/build)

find_library(PICOTLS_OPENSSL_LIBRARY
    NAMES picotls-openssl
    HINTS ${PICOTLS_ROOT_DIR}/build)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  PICOTLS REQUIRED_VARS
  PICOTLS_CORE_LIBRARY
  PICOTLS_MINICRYPTO_LIBRARY
  PICOTLS_OPENSSL_LIBRARY
  PICOTLS_INCLUDE_DIR)

mark_as_advanced(
  PICOTLS_ROOT_DIR
  PICOTLS_LIBRARIES
  PICOTLS_INCLUDE_DIRS)

if(PICOTLS_FOUND)
  set(PICOTLS_LIBRARIES
    ${PICOTLS_CORE_LIBRARY}
    ${PICOTLS_MINICRYPTO_LIBRARY}
    ${PICOTLS_OPENSSL_LIBRARY})
  set(PICOTLS_INCLUDE_DIRS
    ${PICOTLS_INCLUDE_DIR}
    ${MINCRYPTO_INCLUDE_DIR})
  message(STATUS "PICOTLS_LIBRARIES: ${PICOTLS_LIBRARIES}")
  message(STATUS "PICOTLS_INCLUDE_DIRS: ${PICOTLS_INCLUDE_DIRS}")
endif()

mark_as_advanced(PICOTLS_LIBRARIES PICOTLS_INCLUDE_DIRS)

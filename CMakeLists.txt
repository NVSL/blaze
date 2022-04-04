cmake_minimum_required(VERSION 3.9)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
project(Agile)

# Policies
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.12")
  cmake_policy(SET CMP0074 OLD)		# OLD: <PackageName>_ROOT cmake variable or env variable is ignored during find_package
  cmake_policy(SET CMP0075 OLD)
endif()

#GLOBALS
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-march=native)
#set(OPTS -march=native -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -fpermissive -O3)
file(GLOB HEADERS "include/*.h")

include_directories(include)
include_directories(apps/include)
include_directories(include/concurrentqueue)

######################################
# Libraries installed in system path #
######################################

# Threads (Required)
find_package(Threads REQUIRED)
if (Threads_FOUND)
  message(STATUS "Threads found")
else()
  message(WARNING "Threads not found")
endif()

# OpenMP
find_package(OpenMP)
if (OpenMP_FOUND)
  message(STATUS "OpenMP found")
  message(STATUS "  include  : ${OpenMP_CXX_INCLUDE_DIRS}")
  message(STATUS "  libraries: ${OpenMP_CXX_LIBRARIES}")
  include_directories(${OpenMP_CXX_INCLUDE_DIRS})
else()
  message(WARNING "OpenMP not found")
endif()

# VTune
set(VTune_ROOT /opt/intel/oneapi/vtune/latest)
find_package(VTune)
if (VTune_FOUND)
  message(STATUS "VTune found")
  message(STATUS "  include  : ${VTune_INCLUDE_DIRS}")
  message(STATUS "  libraries: ${VTune_LIBRARIES}")
  include_directories(${VTune_INCLUDE_DIRS})
else()
  message(WARNING "VTune not found")
endif()
#set(VTune_ROOT /home/ubuntu/intel/oneapi/vtune/latest)


########################################
# Libraries installed in a custom path #
########################################

# Intel TBB (Required)
set(TBB_ROOT /opt/intel/oneapi/tbb/latest)
include_directories(${TBB_ROOT}/include)
find_library(TBB_LIBRARY libtbb.so
             PATHS ${TBB_ROOT}/lib/intel64/gcc4.8
             NO_DEFAULT_PATH
             REQUIRED)

# Galois (Required)
set(GALOIS_VERSION_MAJOR "5")
set(GALOIS_VERSION_MINOR "0")
set(GALOIS_VERSION_PATCH "0")
set(GALOIS_VERSION ${GALOIS_VERSION_MAJOR}.${GALOIS_VERSION_MINOR}.${GALOIS_VERSION_PATCH})
set(GALOIS_COPYRIGHT_YEAR "2018") # Also in COPYRIGHT

add_definitions(-DGALOIS_VERSION_MAJOR=${GALOIS_VERSION_MAJOR})
add_definitions(-DGALOIS_VERSION_MINOR=${GALOIS_VERSION_MINOR})
add_definitions(-DGALOIS_VERSION_PATCH=${GALOIS_VERSION_PATCH})
add_definitions(-DGALOIS_VERSION=${GALOIS_VERSION_MAJOR}.${GALOIS_VERSION_MINOR}.${GALOIS_VERSION_PATCH})
add_definitions(-DGALOIS_COPYRIGHT_YEAR=${GALOIS_COPYRIGHT_YEAR})

set(GALOIS_ROOT libgalois)
include_directories(${GALOIS_ROOT}/include)

#OUTPUT DIR
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(libllvm)
add_subdirectory(libgalois)
add_subdirectory(src)
add_subdirectory(apps)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-DLINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_definitions(-DDARWIN)
endif ()

# option(BUILD_EXAMPLES "build examples" ON)
# if (BUILD_EXAMPLES)
#     add_subdirectory(examples)
# endif ()

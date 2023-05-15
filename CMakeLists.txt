# CMakeList.txt : CMake project for DSiDLQRGen, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("DSiDLQRGen")

find_package(raylib CONFIG REQUIRED)

find_package(unofficial-mongoose CONFIG REQUIRED)

find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h)
find_library(QRENCODE_LIBRARY_RELEASE qrencode)
find_library(QRENCODE_LIBRARY_DEBUG qrencode)
set(QRENCODE_LIBRARIES optimized ${QRENCODE_LIBRARY_RELEASE} debug ${QRENCODE_LIBRARY_DEBUG})

# Add source to this project's executable.
add_executable (DSiDLQRGen   "src/main.cpp" "src/qrgen.cpp" "src/qrgen.h")
if (MSVC)
	target_link_libraries(DSiDLQRGen PRIVATE raylib)
else()
	target_link_libraries(DSiDLQRGen PRIVATE raylib m asan ubsan ${CMAKE_DL_LIBS} pthread)
endif()
target_include_directories(DSiDLQRGen PRIVATE ${QRENCODE_INCLUDE_DIR})
target_link_libraries(DSiDLQRGen PRIVATE ${QRENCODE_LIBRARIES})
target_link_libraries(DSiDLQRGen PRIVATE unofficial::mongoose::mongoose)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET DSiDLQRGen PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.

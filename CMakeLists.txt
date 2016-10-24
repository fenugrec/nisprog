
cmake_minimum_required(VERSION 2.8.12)

# don't go polluting c:\program files ; this has to go before project()
set (CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Default install prefix")
set (TEMP_INSTPREFIX ${CMAKE_INSTALL_PREFIX})
message("Using install prefix : ${CMAKE_INSTALL_PREFIX}")

project(freediag)

#check if install prefix was clobbered
if ( NOT(${CMAKE_INSTALL_PREFIX} STREQUAL ${TEMP_INSTPREFIX}))
	message(WARNING "Default install prefix has been corrupted ! please report this.")
	#note : if this happens, the alternative is something like
	#if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	# set(installprefix .etc... FORCE)
	#endif()
	#placed after project().
endif ()

include (CheckLibraryExists)
include (CheckFunctionExists)
include (CheckTypeSize)
include (ExternalProject)

set (PKGVERSIONMAJOR "1")
set (PKGVERSIONMINOR "00")
set (PKGVERSION "${PKGVERSIONMAJOR}.${PKGVERSIONMINOR}")
set (SCANTOOL_PROGNAME "nisprog")
#that sets the command-line tool prompt.


###### Compiler flags & build types
if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
	message(STATUS "No build type selected, defaulting to Debug")
	message(STATUS "Available build types : Debug, Release, RelWithDebInfo")
	set(CMAKE_BUILD_TYPE "DEBUG" CACHE STRING
		"Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
		FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
		"MinSizeRel" "RelWithDebInfo")
endif ()


if (CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS_DEBUG "-g")
	set (CMAKE_C_FLAGS_RELEASE "-DNDEBUG")
	set (CMAKE_C_FLAGS_RELWITHDEBINFO "-gsplit-dwarf")
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -pedantic -std=gnu99 -Wstrict-prototypes -Wsign-compare -Wredundant-decls" )
#else ()
	#not sure what we should use on other compilers.
	#set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
endif ()

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

###### Type checks
check_type_size (uint8_t UINT8SIZE)
check_type_size (uint16_t UINT16SIZE)
check_type_size (uint32_t UINT32SIZE)




list(APPEND freediag_cmake_args
       -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
       -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/external
       -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
       -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
)


add_subdirectory(freediag external EXCLUDE_FROM_ALL)


#link_directories(${PROJECT_BINARY_DIR}/freediag-prefix/src/freediag-build/scantool)

#set (NISPROG_SRCS nisprog.c freediag/scantool/scantool_cli.c)
set (NISPROG_SRCS nisprog.c np_cli.c
				scantool_bits.c)

add_executable(nisprog ${NISPROG_SRCS})

target_include_directories(nisprog PUBLIC freediag/scantool)
target_include_directories(nisprog PUBLIC ${PROJECT_BINARY_DIR}/external)

target_link_libraries(nisprog diag freediagcli)


#add_dependencies(nisprog freediag)

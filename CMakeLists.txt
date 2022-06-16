cmake_minimum_required(VERSION 2.8.12)

###### override builtin gcc CFLAGS
# because we don't want NDEBUG

set (CMAKE_USER_MAKE_RULES_OVERRIDE_C "${CMAKE_CURRENT_SOURCE_DIR}/cmake/override_c.cmake")

# don't go polluting c:\program files ; this has to go before project()
set (CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Default install prefix")
set (TEMP_INSTPREFIX ${CMAKE_INSTALL_PREFIX})
message("Using install prefix : ${CMAKE_INSTALL_PREFIX}")

project(nisprog)

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
set (PKGVERSIONMINOR "05")
set (PKGVERSION "${PKGVERSIONMAJOR}.${PKGVERSIONMINOR}")
set (NP_PROGNAME "nisprog")
#that sets the command-line tool prompt.


###### fill metadata file
configure_file (np_conf.h.in np_conf.h)

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


###### add some CFLAGS

if (CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -pedantic -std=gnu99 -Wstrict-prototypes -Wsign-compare -Wredundant-decls" )
#else ()
	#not sure what we should use on other compilers.
	#set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
endif ()

message(STATUS "Generating make/project files for: ${CMAKE_GENERATOR}")

###### Type checks
check_type_size (char CHARSIZE)
check_type_size (uint8_t UINT8SIZE)
check_type_size (uint16_t UINT16SIZE)
check_type_size (uint32_t UINT32SIZE)

if (NOT (${CHARSIZE} EQUAL 1))
	message(FATAL_ERROR "Error, sizeof(char) must be 1 !")
endif()

set(L0LIST "dumb" "dumbtest" "sim")
set(L2LIST "iso9141" "iso14230" "raw")

add_subdirectory(freediag external EXCLUDE_FROM_ALL)



# Add a custom command that produces version.c, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build

ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c
           ${CMAKE_CURRENT_BINARY_DIR}/_version.c
    COMMAND ${CMAKE_COMMAND}
			-D SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gitversion.cmake
	)

set (NISPROG_SRCS nisprog.c np_cli.c nis_backend.c npk_backend.c ssm_backend.c
			scantool_bits.c
			nissutils/cli_utils/nislib.c nissutils/cli_utils/ecuid_list.c
			${CMAKE_CURRENT_BINARY_DIR}/version.c
	)

add_executable(nisprog ${NISPROG_SRCS})

target_include_directories(nisprog PUBLIC freediag/scantool)
target_include_directories(nisprog PUBLIC ${PROJECT_BINARY_DIR})
target_include_directories(nisprog PUBLIC ${PROJECT_BINARY_DIR}/external)

target_link_libraries(nisprog diag freediagcli)


#add_dependencies(nisprog freediag)

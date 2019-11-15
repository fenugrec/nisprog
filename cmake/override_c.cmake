# override builtin CFLAGS from /usr/share/cmake-*/Modules/Compiler/GNU.cmake
if (CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS_INIT "")
	set(CMAKE_C_FLAGS_DEBUG_INIT "-O0 -g")
	set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os")
	set(CMAKE_C_FLAGS_RELEASE_INIT "-O3")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2 -gsplit-dwarf")
endif ()

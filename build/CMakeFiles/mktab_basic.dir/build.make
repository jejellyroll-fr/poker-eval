# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/jd/Documents/github/poker-eval

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jd/Documents/github/poker-eval/build

# Include any dependencies generated for this target.
include CMakeFiles/mktab_basic.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/mktab_basic.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/mktab_basic.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mktab_basic.dir/flags.make

CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o: CMakeFiles/mktab_basic.dir/flags.make
CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o: ../lib/mktab_basic.c
CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o: CMakeFiles/mktab_basic.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o -MF CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o.d -o CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o -c /home/jd/Documents/github/poker-eval/lib/mktab_basic.c

CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jd/Documents/github/poker-eval/lib/mktab_basic.c > CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.i

CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jd/Documents/github/poker-eval/lib/mktab_basic.c -o CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.s

# Object files for target mktab_basic
mktab_basic_OBJECTS = \
"CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o"

# External object files for target mktab_basic
mktab_basic_EXTERNAL_OBJECTS =

mktab_basic: CMakeFiles/mktab_basic.dir/lib/mktab_basic.c.o
mktab_basic: CMakeFiles/mktab_basic.dir/build.make
mktab_basic: libpoker_lib.a
mktab_basic: CMakeFiles/mktab_basic.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable mktab_basic"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mktab_basic.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/mktab_basic.dir/build: mktab_basic
.PHONY : CMakeFiles/mktab_basic.dir/build

CMakeFiles/mktab_basic.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mktab_basic.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mktab_basic.dir/clean

CMakeFiles/mktab_basic.dir/depend:
	cd /home/jd/Documents/github/poker-eval/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build/CMakeFiles/mktab_basic.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/mktab_basic.dir/depend


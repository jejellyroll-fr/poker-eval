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
include CMakeFiles/shortdecktest.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/shortdecktest.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/shortdecktest.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/shortdecktest.dir/flags.make

CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o: CMakeFiles/shortdecktest.dir/flags.make
CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o: ../tests/shortdecktest.c
CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o: CMakeFiles/shortdecktest.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o -MF CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o.d -o CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o -c /home/jd/Documents/github/poker-eval/tests/shortdecktest.c

CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jd/Documents/github/poker-eval/tests/shortdecktest.c > CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.i

CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jd/Documents/github/poker-eval/tests/shortdecktest.c -o CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.s

# Object files for target shortdecktest
shortdecktest_OBJECTS = \
"CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o"

# External object files for target shortdecktest
shortdecktest_EXTERNAL_OBJECTS =

shortdecktest: CMakeFiles/shortdecktest.dir/tests/shortdecktest.c.o
shortdecktest: CMakeFiles/shortdecktest.dir/build.make
shortdecktest: libpoker_lib.a
shortdecktest: CMakeFiles/shortdecktest.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable shortdecktest"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/shortdecktest.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/shortdecktest.dir/build: shortdecktest
.PHONY : CMakeFiles/shortdecktest.dir/build

CMakeFiles/shortdecktest.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/shortdecktest.dir/cmake_clean.cmake
.PHONY : CMakeFiles/shortdecktest.dir/clean

CMakeFiles/shortdecktest.dir/depend:
	cd /home/jd/Documents/github/poker-eval/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build/CMakeFiles/shortdecktest.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/shortdecktest.dir/depend


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
include CMakeFiles/mktab_joker.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/mktab_joker.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/mktab_joker.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mktab_joker.dir/flags.make

CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o: CMakeFiles/mktab_joker.dir/flags.make
CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o: ../lib/mktab_joker.c
CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o: CMakeFiles/mktab_joker.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o -MF CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o.d -o CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o -c /home/jd/Documents/github/poker-eval/lib/mktab_joker.c

CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jd/Documents/github/poker-eval/lib/mktab_joker.c > CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.i

CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jd/Documents/github/poker-eval/lib/mktab_joker.c -o CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.s

# Object files for target mktab_joker
mktab_joker_OBJECTS = \
"CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o"

# External object files for target mktab_joker
mktab_joker_EXTERNAL_OBJECTS =

mktab_joker: CMakeFiles/mktab_joker.dir/lib/mktab_joker.c.o
mktab_joker: CMakeFiles/mktab_joker.dir/build.make
mktab_joker: libpoker_lib.a
mktab_joker: CMakeFiles/mktab_joker.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jd/Documents/github/poker-eval/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable mktab_joker"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mktab_joker.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/mktab_joker.dir/build: mktab_joker
.PHONY : CMakeFiles/mktab_joker.dir/build

CMakeFiles/mktab_joker.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mktab_joker.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mktab_joker.dir/clean

CMakeFiles/mktab_joker.dir/depend:
	cd /home/jd/Documents/github/poker-eval/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build/CMakeFiles/mktab_joker.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/mktab_joker.dir/depend


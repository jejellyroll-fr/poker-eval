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

# Utility rule file for bug1823.

# Include any custom commands dependencies for this target.
include CMakeFiles/bug1823.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/bug1823.dir/progress.make

CMakeFiles/bug1823: pokenum
	cd /home/jd/Documents/github/poker-eval/tests && /bin/bash /home/jd/Documents/github/poker-eval/tests/bug1823

bug1823: CMakeFiles/bug1823
bug1823: CMakeFiles/bug1823.dir/build.make
.PHONY : bug1823

# Rule to build all files generated by this target.
CMakeFiles/bug1823.dir/build: bug1823
.PHONY : CMakeFiles/bug1823.dir/build

CMakeFiles/bug1823.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/bug1823.dir/cmake_clean.cmake
.PHONY : CMakeFiles/bug1823.dir/clean

CMakeFiles/bug1823.dir/depend:
	cd /home/jd/Documents/github/poker-eval/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build /home/jd/Documents/github/poker-eval/build/CMakeFiles/bug1823.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/bug1823.dir/depend


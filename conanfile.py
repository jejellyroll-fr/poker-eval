from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.files import copy
import os

class PokerEvalConan(ConanFile):
    name = "poker-eval"
    version = "1.2.0"
    license = "BSD-3-Clause"
    author = "Poker-eval Project"
    url = "https://github.com/poker-eval/poker-eval"
    description = "Fast poker hand evaluation library"
    topics = ("poker", "cards", "games", "evaluation")
    
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_gpu": [True, False],
        "with_python": [True, False],
        "with_java": [True, False],
        "with_tools": [True, False],
        "enable_lto": [True, False],
        "use_five_cards": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_gpu": False,
        "with_python": False,
        "with_java": False,
        "with_tools": True,
        "enable_lto": True,
        "use_five_cards": False
    }
    
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "examples/*", "tests/*", "gpu/*", "bindings/*"
    
    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
    
    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        self.settings.rm_safe("compiler.libcxx")
        self.settings.rm_safe("compiler.cppstd")
    
    def requirements(self):
        if self.options.with_gpu:
            # Add GPU-related requirements if needed
            pass
    
    def build_requirements(self):
        # Build tools requirements
        pass
    
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.variables["BUILD_GPU"] = self.options.with_gpu
        tc.variables["BUILD_BINDINGS"] = self.options.with_python or self.options.with_java
        tc.variables["BUILD_PYTHON_BINDING"] = self.options.with_python
        tc.variables["BUILD_JAVA_BINDING"] = self.options.with_java
        tc.variables["BUILD_EXAMPLES"] = False
        tc.variables["BUILD_TESTS"] = False
        tc.variables["ENABLE_LTO"] = self.options.enable_lto
        tc.variables["USE_FIVE_CARDS"] = self.options.use_five_cards
        tc.generate()
        
        deps = CMakeDeps(self)
        deps.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    
    def package(self):
        cmake = CMake(self)
        cmake.install()
    
    def package_info(self):
        self.cpp_info.libs = ["poker-eval"]
        self.cpp_info.includedirs = ["include/poker-eval"]
        
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs = ["m", "pthread"]
        
        if not self.options.shared:
            self.cpp_info.defines = ["POKER_EVAL_STATIC"]
        
        # Add components
        self.cpp_info.components["core"].libs = ["poker-eval"]
        self.cpp_info.components["core"].includedirs = ["include/poker-eval"]
        
        if self.options.with_gpu:
            self.cpp_info.components["gpu"].libs = ["poker-gpu"]
            self.cpp_info.components["gpu"].requires = ["core"]
        
        # Set the package folder for tools
        if self.options.with_tools:
            self.cpp_info.bindirs = ["bin"]

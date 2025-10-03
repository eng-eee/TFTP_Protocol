from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class DatabaseAppConan(ConanFile):
    name = "DataBaseApp"
    version = "0.1"
    author = "Mert"
    url = "https://github.com/eng-eee"
    description = "A simple C++ database application using SQLite"
    topics = ("c++", "sqlite", "database")

    # Settings: compiler, architecture, etc.
    settings = "os", "compiler", "build_type", "arch"

    # Dependencies
    requires = (
        "gtest/1.14.0",
        "sqlite3/3.46.0"
    )

    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = "src/*", "CMakeLists.txt", "test/*"
    
    # Build system: CMake
    def layout(self):
        cmake_layout(self)

    def build(self):
        import os
        if not os.path.exists(self.build_folder):
            raise ("No build folder found. Please run 'conan install' first.")
        cmake = CMake(self)
        cmake.configure()
        print("Building the project...")
        cmake.build()
        # Run unit tests automatically after build
        import subprocess
        print("Running unit tests...")
        # subprocess.run([f"{self.build_folder}/test/unit_test"], check=True)
        subprocess.run(["ctest", "--output-on-failure"], check=True)

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["database_app"]

    def test(self):
        if not self.in_local_cache:
            return
        import os
        if not os.path.exists(self.build_folder):
            raise ("No build folder found. Please run 'conan install' first.")
        cmake = CMake(self)
        cmake.configure()
        print("Running unit tests...")
        import subprocess
        subprocess.run(["ctest", "--output-on-failure"], check=True)




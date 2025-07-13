# Minimum required CMake version
cmake_minimum_required(VERSION 3.10)

# Define the overall project name
project(ChatApplication LANGUAGES CXX)

# Set the C++ standard for the entire project
# C++11 is a good minimum for modern C++ features like std::thread
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE) # Make it mandatory
set(CMAKE_CXX_EXTENSIONS OFF)         # Use standard C++ features only

# Set a common output directory for all executables and libraries
# This will put all compiled binaries into a 'bin' folder inside your build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # For static libraries

# Add subdirectories. CMake will process their CMakeLists.txt files.
# The order matters if there are dependencies (e.g., utils must be built before client/server)
add_subdirectory(socketUtils)
add_subdirectory(socketServer)
add_subdirectory(socketClient)

# Define a static library target named 'socketUtils'
# This will compile utils.cpp into a library file (e.g., socketUtils.lib on Windows)
add_library(socketUtils STATIC socketutil.cpp)
target_include_directories(socketUtils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

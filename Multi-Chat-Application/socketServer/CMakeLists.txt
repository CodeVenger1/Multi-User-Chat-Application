# Define the server executable target
# This will compile server.cpp into an executable named 'ChatServer'
add_executable(ChatServer main.cpp)

# Link the server executable to the socketUtils library
# This makes functions from socketUtils (like printWinsockError) available to ChatServer
target_link_libraries(ChatServer PRIVATE socketUtils)

# Link the Winsock library for Windows.
# CMake automatically handles this for network functions, but explicit linking
# is good practice and ensures it's always included.
# On Windows, the library name is Ws2_32.
target_link_libraries(ChatServer PRIVATE Ws2_32)

# Add include directory for socketUtils so server.cpp can find utils.h
# This is typically handled by target_link_libraries if socketUtils is PUBLIC,
# but explicitly adding it ensures clarity.
target_include_directories(ChatServer PRIVATE ${CMAKE_SOURCE_DIR}/socketUtils)

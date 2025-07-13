# Define the client executable target
# This will compile client.cpp into an executable named 'ChatClient'
add_executable(ChatClient client.cpp)

# Link the client executable to the socketUtils library
target_link_libraries(ChatClient PRIVATE socketUtils)

# Link the Winsock library for Windows.
target_link_libraries(ChatClient PRIVATE Ws2_32)

# Add include directory for socketUtils so client.cpp can find utils.h
target_include_directories(ChatClient PRIVATE ${CMAKE_SOURCE_DIR}/socketUtils)

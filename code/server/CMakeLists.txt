
project(server)

set(SRC_FILES 
posix_net_client_set.cpp
game.cpp
net_events.cpp
net_commands.cpp
posix_net.cpp
main.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE common)
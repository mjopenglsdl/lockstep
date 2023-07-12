
project(client)

# find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)

set(SRC_FILES 
game.cpp
net_events.cpp
net_commands.cpp
posix_net.cpp
opengl.cpp
interpolation.cpp
main.cpp
)


message( "OpenGL_LIBRARIES: ${OpenGL_LIBRARIES}")
message( "GLUT_LIBRARIES: ${GLUT_LIBRARIES}")

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE common)


target_link_libraries(${PROJECT_NAME} PRIVATE 
${GLUT_LIBRARIES} 
# ${OpenGL_LIBRARIES} 
# linux opengl
GL
)
project(common LANGUAGES C)

set(SRC_FILES 
simulation.cpp
net_messages.cpp
conversion.cpp
posix_net.cpp
order_serialization.cpp
)

add_library(${PROJECT_NAME} ${SRC_FILES})

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PUBLIC base spdlog::spdlog)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
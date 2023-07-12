project(base)

set(SRC_FILES 
"chunk_ring_buffer.c"
"byte_ring_buffer.c"
"buf_view.c"
"assert.c"
"seq_write.c"
"memory_arena.c"
"int_seq.c"
"chunk_list.c"
"math.cpp"
)

add_library(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
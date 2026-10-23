file(GLOB PIPES_SRC "*.hpp" "*.cpp")

file(GLOB_RECURSE RESOURCE_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts/*
)

add_executable(PIPES ${PIPES_SRC})

find_package(Threads REQUIRED)
target_link_libraries(PIPES PRIVATE Threads::Threads)
target_link_libraries(PIPES PRIVATE ImGui-SFML::ImGui-SFML)

set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/fonts)
add_custom_command(TARGET PIPES POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts
    ${OUTPUT_DIR}
)
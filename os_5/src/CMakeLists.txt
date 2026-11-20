file(GLOB OS5_SRC "*.hpp" "*.cpp")

add_executable(OS5 ${OS5_SRC})

# Packages
find_package(Threads REQUIRED)

# Libraries
find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
find_library(DISKARBITRATION_FRAMEWORK DiskArbitration REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

target_link_libraries(OS5 PRIVATE Threads::Threads)
target_link_libraries(OS5 PRIVATE ImGui-SFML::ImGui-SFML)
target_link_libraries(OS5 PRIVATE ${IOKIT_FRAMEWORK} ${DISKARBITRATION_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})

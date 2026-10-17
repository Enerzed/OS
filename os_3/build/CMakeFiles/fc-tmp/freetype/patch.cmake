cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

message(VERBOSE "Executing patch step for freetype")

block(SCOPE_FOR VARIABLES)

execute_process(
  WORKING_DIRECTORY "/Users/nikita/code/os/os_3/build/_deps/freetype-src"
  COMMAND_ERROR_IS_FATAL LAST
  COMMAND  [====[/opt/homebrew/bin/cmake]====] [====[-DFREETYPE_DIR=/Users/nikita/code/os/os_3/build/_deps/freetype-src]====] [====[-P]====] [====[/Users/nikita/code/os/os_3/build/_deps/sfml-src/tools/freetype/PatchFreetype.cmake]====]
)

endblock()

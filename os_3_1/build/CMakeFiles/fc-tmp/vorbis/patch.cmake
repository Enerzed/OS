cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

message(VERBOSE "Executing patch step for vorbis")

block(SCOPE_FOR VARIABLES)

execute_process(
  WORKING_DIRECTORY "C:/Users/laske/Desktop/Лабы/Проектирование операционных систем/OS/os_3_1/build/_deps/vorbis-src"
  COMMAND_ERROR_IS_FATAL LAST
  COMMAND  [====[C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe]====] [====[-DVORBIS_DIR=C:/Users/laske/Desktop/Лабы/Проектирование операционных систем/OS/os_3_1/build/_deps/vorbis-src]====] [====[-P]====] [====[C:/Users/laske/Desktop/Лабы/Проектирование операционных систем/OS/os_3_1/build/_deps/sfml-src/tools/vorbis/PatchVorbis.cmake]====]
)

endblock()

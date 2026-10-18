message(STATUS "Fetching SFML...")

# Делаем обязательными следующие модули
set(SFML_BUILD_GRAPHICS TRUE)
set(SFML_BUILD_SYSTEM TRUE)
set(SFML_BUILD_WINDOW TRUE)

FetchContent_MakeAvailable(sfml)
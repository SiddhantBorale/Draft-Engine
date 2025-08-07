cmake_minimum_required(VERSION 3.16)
project(DraftAI)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---- Qt 6 Widgets ----
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---- Fetch Clipper2 automatically ----
include(FetchContent)
FetchContent_Declare(
    clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/clipper2.git
    GIT_TAG        main  # Clipper2's default branch (was 'master' previously)
)
FetchContent_MakeAvailable(clipper2)

# ---- Source files ----
file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.h
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
)

# Clipper2 is header‑only, so just add its headers
FetchContent_GetProperties(clipper2 SOURCE_DIR clipper2_SOURCE_DIR)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${clipper2_SOURCE_DIR}/CPP
)


target_include_directories(${PROJECT_NAME} PRIVATE
    src # your own headers
)
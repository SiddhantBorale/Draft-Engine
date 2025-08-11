cmake_minimum_required(VERSION 3.16)
project(DraftAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg SvgWidgets Network)

# FetchContent
include(FetchContent)

# Clipper2 (header-only)
FetchContent_Declare(
  clipper2
  GIT_REPOSITORY https://github.com/AngusJohnson/clipper2.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(clipper2)

# Sources / target
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(${PROJECT_NAME} ${SRC})

# Link
target_link_libraries(${PROJECT_NAME}
  PRIVATE 
  Qt6::Widgets 
  Qt6::Svg 
  Qt6::SvgWidgets
  Qt6::Network
)

target_sources(DraftAI PRIVATE
    src/canvas/DrawingCanvas.cpp
    src/canvas/DrawingCanvas.h
    src/dim/LinearDimItem.cpp
    src/dim/LinearDimItem.h
    src/dim/AnchorPoint.h
)

# Includes
target_include_directories(${PROJECT_NAME} PRIVATE
  src
  ${clipper2_SOURCE_DIR}/CPP
)

cmake_minimum_required(VERSION 3.16)
project(DraftAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ─── Qt ──────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network)

# ─── FetchContent ───────────────────────────────────────────
include(FetchContent)

# --- Clipper2 ----------------------------------------------
FetchContent_Declare(
  clipper2
  GIT_REPOSITORY https://github.com/AngusJohnson/clipper2.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(clipper2)

# --- libdxfrw  (headers only) -------------------------------
FetchContent_Declare(
  dxfrw
  GIT_REPOSITORY https://github.com/LibreCAD/libdxfrw.git
  GIT_TAG        master
  CONFIGURE_COMMAND ""
  BUILD_COMMAND     ""
  INSTALL_COMMAND   ""
)
FetchContent_Populate(dxfrw)        # download only

# ─── Sources / target ───────────────────────────────────────
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS src/*.cpp src/*.h)

# Add single implementation file that defines dxfRW symbols
list(APPEND SRC ${dxfrw_SOURCE_DIR}/src/dxfRW.cpp)

add_executable(${PROJECT_NAME} ${SRC})

# ─── Link Qt libs ───────────────────────────────────────────
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt6::Widgets
        Qt6::Svg
        Qt6::Network
)

# ─── Include paths ─────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${clipper2_SOURCE_DIR}/CPP
    ${dxfrw_SOURCE_DIR}
    ${dxfrw_SOURCE_DIR}/src
    ${dxfrw_SOURCE_DIR}/intern
)

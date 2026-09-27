cmake_minimum_required(VERSION 3.16)
project(judger-gui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
  find_package(Qt5 5.15 COMPONENTS Widgets REQUIRED)
endif()

# minizip (from zlib contrib — sources bundled with zlib's git repo)
FetchContent_GetProperties(zlib_src SOURCE_DIR ZLIB_SRC_DIR)
set(MINIZIP_DIR ${ZLIB_SRC_DIR}/contrib/minizip)
add_library(minizip STATIC
    ${MINIZIP_DIR}/ioapi.c
    ${MINIZIP_DIR}/unzip.c
    ${MINIZIP_DIR}/zip.c
    ${MINIZIP_DIR}/mztools.c
)
target_include_directories(minizip PUBLIC ${MINIZIP_DIR})
target_link_libraries(minizip PUBLIC ZLIB::ZLIB)

set(SOURCES
    main.cpp
    MainWindow.cpp
    ContestModel.cpp
    ContestData.cpp
    TestDataDialog.cpp
    CompilerDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/AppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../JudgeBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ProcessIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../JudgeAPI.cpp
)

set(HEADERS
    MainWindow.h
    ContestModel.h
    ContestData.h
    TestDataDialog.h
    CompilerDialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/AppConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../JudgeBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ProcessIO.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../JudgeAPI.h
)

add_executable(judger_gui ${SOURCES} ${HEADERS})

target_include_directories(judger_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(Qt6_FOUND)
  target_link_libraries(judger_gui PRIVATE Qt6::Widgets tinyxml2::tinyxml2 minizip nlohmann_json::nlohmann_json ZLIB::ZLIB plog::plog)
else()
  target_link_libraries(judger_gui PRIVATE Qt5::Widgets tinyxml2::tinyxml2 minizip nlohmann_json::nlohmann_json ZLIB::ZLIB plog::plog)
endif()

install(TARGETS judger_gui RUNTIME DESTINATION bin)

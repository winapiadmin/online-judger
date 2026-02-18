cmake_minimum_required(VERSION 3.16)
project(judger LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(_CLI11_FINDPACKAGE "Use system CLI11 if available" ON)
option(_cpptrace_FINDPACKAGE "Use system cpptrace if available" ON)
option(_yaml-cpp_FINDPACKAGE "Use system yaml-cpp if available" ON)
option(_plog_FINDPACKAGE "Use system plog if available" ON)
option(_tomlplusplus_FINDPACKAGE "Use system toml++ if available" ON)
option(_nlohmann_json_FINDPACKAGE "Use system nlohmann/json if available" ON)
option(_ZLIB_FINDPACKAGE "Use system zlib if available" ON)
option(_TinyXML2_FINDPACKAGE "Use system TinyXML2 if available" ON)
option(_efsw_FINDPACKAGE "Use system efsw (https://github.com/SpartanJ/efsw) if available" ON)

# =====================
# Dependencies: find or fetch
# =====================

include(FetchContent)

# ---- CLI11 ----
if (_CLI11_FINDPACKAGE)
  find_package(CLI11 QUIET)
endif()
if (NOT CLI11_FOUND)
  FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG main
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(CLI11)
else()
  message(STATUS "Found CLI11")
endif()

# ---- cpptrace ----
if (_cpptrace_FINDPACKAGE)
  find_package(cpptrace QUIET)
endif()
if (NOT cpptrace_FOUND)
  FetchContent_Declare(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG main
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(cpptrace)
else()
  message(STATUS "Found cpp-trace")
endif()

# ---- yaml-cpp ----
if (_yaml-cpp_FINDPACKAGE)
  find_package(yaml-cpp QUIET)
endif()
if (NOT yaml-cpp_FOUND)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG master
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(yaml-cpp)
else()
  message(STATUS "Found yaml-cpp")
endif()

# ---- plog ----
if (_plog_FINDPACKAGE)
  find_package(plog QUIET)
endif()
if (NOT plog_FOUND)
  FetchContent_Declare(
    plog
    GIT_REPOSITORY https://github.com/SergiusTheBest/plog.git
    GIT_TAG master
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(plog)
else()
  message(STATUS "Found plog")
endif()

# ---- toml++ ----
if (_tomlplusplus_FINDPACKAGE)
  find_package(tomlplusplus QUIET)
endif()
if (NOT tomlplusplus_FOUND)
  FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(tomlplusplus)
else()
  message(STATUS "Found tomlplusplus")
endif()

# ---- nlohmann/json ----
if (_nlohmann_json_FINDPACKAGE)
  find_package(nlohmann_json QUIET)
endif()
if (NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG develop
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(json)
else()
  message(STATUS "Found nlohmann_json")
endif()

# ---- zlib ----
if (_ZLIB_FINDPACKAGE)
  find_package(ZLIB QUIET)
endif()
if (NOT TARGET ZLIB::ZLIB)
  FetchContent_Declare(
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    GIT_TAG develop
    FIND_PACKAGE_ARGS NAMES ZLIB
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(zlib)
  if(TARGET zlibstatic)
      add_library(ZLIB::ZLIB ALIAS zlibstatic)
  elseif (TARGET zlib)
      add_library(ZLIB::ZLIB ALIAS zlib)
  endif()
else()
  message(STATUS "Found ZLIB")
endif()

# ---- tinyxml2 ----
if (_TinyXML2_FINDPACKAGE)
  find_package(TinyXML2 QUIET)
endif()
if (NOT TARGET tinyxml2::tinyxml2)
  FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        master
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(tinyxml2)
else()
  message(STATUS "Found TinyXML2")
endif()
# ---- efsw ----
if (_efsw_FINDPACKAGE)
  find_package(efsw QUIET)
endif()
if (NOT TARGET efsw-static)
  FetchContent_Declare(
    efsw
    GIT_REPOSITORY https://github.com/SpartanJ/efsw.git
    GIT_TAG        master
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(efsw)
else()
  message(STATUS "Found efsw")
endif()
# =====================
# Targets
# =====================

add_library(judge SHARED judge.c)
add_library(C1LinesWordsIgnoreCase SHARED C1LinesWordsIgnoreCase.c)

add_executable(main_judger oj_core.cpp parsers.cpp ProcessIO.cpp JudgeAPI.cpp JudgeBackend.cpp SubmissionWatcher.cpp)

target_link_libraries(main_judger
  PRIVATE
    CLI11::CLI11
    cpptrace::cpptrace
    yaml-cpp
    plog::plog
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
    tinyxml2::tinyxml2
    efsw-static
)

add_compile_definitions(TOML_ENABLE_WINDOWS_COMPAT PLOG_ENABLE_WCHAR_INPUT)

install(TARGETS main_judger
        RUNTIME DESTINATION bin)

install(TARGETS judge C1LinesWordsIgnoreCase
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(FILES JudgeAPI.h
        DESTINATION include/judger)

cmake_minimum_required(VERSION 3.16)
project(ehentai_crawler CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ehentai_crawler
    ehentai_crawler/entry.cpp
    ehentai_crawler/crawler.cpp
    ehentai_crawler/ehentai.cpp
    ehentai_crawler/html.cpp
    ehentai_crawler/http.cpp
)

target_include_directories(ehentai_crawler PRIVATE ehentai_crawler)

find_package(CURL REQUIRED)
target_link_libraries(ehentai_crawler PRIVATE CURL::libcurl)

# vcpkg ships gumbo with a config file; distro packages only have the plain
# header and library, so fall back to locating them by hand.
find_package(unofficial-gumbo CONFIG QUIET)
if(unofficial-gumbo_FOUND)
    target_link_libraries(ehentai_crawler PRIVATE unofficial::gumbo::gumbo)
else()
    find_path(GUMBO_INCLUDE_DIR gumbo.h REQUIRED)
    find_library(GUMBO_LIBRARY NAMES gumbo REQUIRED)
    target_include_directories(ehentai_crawler PRIVATE ${GUMBO_INCLUDE_DIR})
    target_link_libraries(ehentai_crawler PRIVATE ${GUMBO_LIBRARY})
endif()

find_package(Threads REQUIRED)
target_link_libraries(ehentai_crawler PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ehentai_crawler PRIVATE /W3 /utf-8)
else()
    target_compile_options(ehentai_crawler PRIVATE -Wall -Wextra)
endif()

cmake_minimum_required(VERSION 3.6)
project(myalloc C)

set(HEADER_FILES myalloc.h)
set(SOURCE_FILES alligator.c myalloc.c)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_executable(alligator ${SOURCE_FILES})

target_link_libraries(alligator Threads::Threads)
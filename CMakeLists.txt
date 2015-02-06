cmake_minimum_required(VERSION 2.8.4)
project(JudgeServerC)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

set(SOURCE_FILES
    main.c
    server.c
    server.h
    solutions_db.c
    solutions_db.h
    thread_pool.c
    thread_pool.h
    uthash.h
    utils.c
    utils.h
    utlist.h)
include_directories(/usr/include)
add_executable(JudgeServerC ${SOURCE_FILES})
link_directories(/usr/lib)
target_link_libraries(JudgeServerC mysqlclient)
target_link_libraries(JudgeServerC pthread)

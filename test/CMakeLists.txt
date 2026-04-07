add_executable(task_test task_test.cpp)
target_include_directories(task_test PRIVATE ../src/include)
target_link_libraries(task_test PRIVATE Threads::Threads)
add_test(NAME task_test COMMAND task_test)

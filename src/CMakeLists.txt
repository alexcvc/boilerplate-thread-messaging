### Set project name
set(TargetName threadMsg)

### List of CPP (source) library files.
set(${TargetName}_SRC
   src/daemon.cpp
   src/main.cpp
)

set(${TargetName}_HDR
   include/messaging/messageSender.hpp
   include/messaging/messageReceiver.hpp
   include/messaging/messageBase.hpp
   include/messaging/messageQueue.hpp
   include/messaging/task.hpp
   include/threading/spScRingBuffer.hpp
   include/threading/threadSafeQueue.hpp
   include/time/stopTimer.hpp
   include/daemon.hpp
   include/daemonConfig.hpp
   include/workerBase.hpp
)

#######################################################
### add executable
#######################################################
add_executable(${TargetName}
   ${${TargetName}_SRC}
   ${${TargetName}_HDR}
   include/threadManager.hpp
)

target_link_options(${TargetName}
   PRIVATE
   $<$<CONFIG:Release>:-s>
)

target_compile_definitions(${TargetName}
   PRIVATE
   $<$<AND:$<CONFIG:Debug>,$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>>:USE_DEBUG_X86_64>
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  target_compile_definitions(${TargetName} PRIVATE USE_DEBUG_X86_64)
endif ()

target_include_directories(${TargetName} PRIVATE
   ${CMAKE_CURRENT_BINARY_DIR}
   "${CMAKE_CURRENT_SOURCE_DIR}/include"
   "${CMAKE_CURRENT_SOURCE_DIR}/include/messaging"
   "${CMAKE_CURRENT_SOURCE_DIR}/include/threading"
   "${CMAKE_CURRENT_SOURCE_DIR}/include/time"
)

target_link_libraries(${TargetName} PRIVATE
   Threads::Threads
)

#######################################################
# post build copy optional
#######################################################
if ((NOT ${CMAKE_POSTBUILD_OUTPUT_DIRECTORY}) AND (IS_DIRECTORY ${CMAKE_POSTBUILD_OUTPUT_DIRECTORY}))
  message(STATUS "Target ${TargetName} will be installed in ${CMAKE_POSTBUILD_OUTPUT_DIRECTORY}")
  # Copy target file to another location in a post build step in
  add_custom_command(TARGET ${TargetName} POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TargetName}> ${CMAKE_POSTBUILD_OUTPUT_DIRECTORY}
  )
endif ()

install(TARGETS ${TargetName} DESTINATION bin)

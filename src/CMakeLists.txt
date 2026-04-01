### Set project name
set(TargetName goose-slave)

### List of CPP (source) library files.
set(${TargetName}_SRC
   src/ApplicationWorker.cpp
   src/Daemon.cpp
   src/GooseContext.cpp
   src/GooseWorker.cpp
   src/main.cpp
   src/PipeEventWorker.cpp
   src/ReadXmlConfig.cpp
   src/ShmProcImageWorker.cpp
   src/time/TimeUtilities.cpp
)

set(${TargetName}_SLAVE_SRC
   slave/slaves.c
)

set(${TargetName}_HDR
   include/messaging/Sender.hpp
   include/messaging/Receiver.hpp
   include/messaging/Message.hpp
   include/messaging/MessageQueue.hpp
   include/threading/JThread.hpp
   include/threading/SpscRingBuffer.hpp
   include/GooseContext.hpp
   include/GooseWorker.hpp
   include/PipeEventWorker.hpp
   include/ShmProcImageWorker.hpp
   include/WorkerBase.hpp
   include/time/StopTimer.hpp
   include/time/TimeUtilities.hpp
   include/ApplicationWorker.hpp
   include/Daemon.hpp
   include/DaemonConfig.hpp
   include/DataTypes.hpp
   include/ReadXmlConfig.hpp
)
set(${TargetName}_SLAVE_HDR
   slave/custom.h
   slave/slaves.h
   slave/slaves_define.h
)

#######################################################
### Version handling
#######################################################

configure_file("${CMAKE_SOURCE_DIR}/.cmake/version.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")
configure_file("${CMAKE_SOURCE_DIR}/.cmake/version.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/version.hpp")

#######################################################
### add executable
#######################################################
add_executable(${TargetName}
   ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
   ${CMAKE_CURRENT_BINARY_DIR}/version.hpp
   ${${TargetName}_SRC}
   ${${TargetName}_SLAVE_SRC}
   ${${TargetName}_HDR}
   ${${TargetName}_SLAVE_HDR}
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
   "${CMAKE_CURRENT_SOURCE_DIR}/rtw"
   "${CMAKE_CURRENT_SOURCE_DIR}/slave"
)

target_include_directories(${TargetName} PRIVATE
   ${CMAKE_SOURCE_DIR}/lib/iecgoose/include
)

target_link_libraries(${TargetName} PRIVATE
   Threads::Threads
   iecgoose
   pugixml::pugixml
   spdlog::spdlog_header_only
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

# Remote copy after build
#-DREMOTE_IP="192.168.1.97"
#-DREMOTE_USER="root"
#-DREMOTE_PASSWORD="32iemTWvlGWT9sijKmUukh9RWBaWqnXYIJaraqDhy20="
#-DREMOTE_PATH="/appfs/test/"

if (REMOTE_IP AND REMOTE_USER AND REMOTE_PATH)
  message(STATUS "Target ${TargetName} will be copied to ${REMOTE_USER}@${REMOTE_IP}:${REMOTE_PATH}")
  add_custom_command(
     TARGET ${TargetName}
     POST_BUILD
     COMMAND sshpass -p "${REMOTE_PASSWORD}" scp "$<TARGET_FILE:${TargetName}>"
     ${REMOTE_USER}@${REMOTE_IP}:${REMOTE_PATH}
     COMMENT "Copying ${TargetName} to ${REMOTE_USER}@${REMOTE_IP}:${REMOTE_PATH}"
  )
endif ()

install(TARGETS ${TargetName} DESTINATION bin)

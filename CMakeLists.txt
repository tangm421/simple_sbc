cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(simpleSBC VERSION 0.0.1 LANGUAGES CXX)

#cmake_policy(SET CMP0048 NEW)

set(RESIP_INC_DIR "" CACHE PATH "RESIP include path")
set(RESIP_LIB_DIR "" CACHE PATH "RESIP library path")
set(POPT_INC_DIR "" CACHE PATH "popt include path")
set(POPT_LIB_DIR "" CACHE PATH "popt library path")

if((NOT EXISTS ${RESIP_INC_DIR}) OR (NOT EXISTS ${RESIP_LIB_DIR}))
  MESSAGE(SEND_ERROR "The RESIP include and library path must be specified")
endif()

message(STATUS RESIP_INC_DIR:${RESIP_INC_DIR})
message(STATUS RESIP_LIB_DIR:${RESIP_LIB_DIR})
message(STATUS POPT_INC_DIR:${POPT_INC_DIR})
message(STATUS POPT_LIB_DIR:${POPT_LIB_DIR})


include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
  message(SEND_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

link_directories(${RESIP_LIB_DIR} ${POPT_LIB_DIR})
add_executable(${PROJECT_NAME} cmd_option.cpp  cmd_option.h  main.cpp  simple_sbc.cpp  simple_sbc.h  ss_subsystem.cpp  ss_subsystem.h )
target_include_directories(${PROJECT_NAME} PRIVATE ${RESIP_INC_DIR} ${POPT_INC_DIR})
#target_link_directories(${PROJECT_NAME} PRIVATE ${RESIP_LIB_DIR}) # repace by link_directories for older cmake

if(WIN32)
  set(RESIP_LIB_ALL ares rutil resiprocate dum)
else()
  set(RESIP_LIB_ALL dum resip rutil resipares pthread)
endif()

if(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ${RESIP_LIB_ALL} popt Ws2_32.lib Iphlpapi.lib winmm.lib Dnsapi.lib)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE ${RESIP_LIB_ALL} popt pthread)
endif()

# CMakeList.txt: RTSNet 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

project("TinyNet")

IF (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Darwin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
endif()

include_directories(${PROJECT_SOURCE_DIR}/Include)

file(GLOB MainSource 
	${PROJECT_SOURCE_DIR}/Include/*.h
	${PROJECT_SOURCE_DIR}/Source/*.cpp
)

source_group(Main FILES ${MainSource})

add_library(${PROJECT_NAME} SHARED 
	${MainSource}
)


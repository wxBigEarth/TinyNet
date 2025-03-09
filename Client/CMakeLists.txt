# CMakeList.txt: RTSDataStream 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

#project ("Client")

# 将源代码添加到此项目的可执行文件。
add_executable (Client "Client.cpp" "Client.h")

# TODO: 如有需要，请添加测试并安装目标。
include_directories(
	${PROJECT_SOURCE_DIR}/TinyNet/Include
)

# TODO: 如有需要，请添加测试并安装目标。

IF (CMAKE_SYSTEM_NAME MATCHES "Windows")
	target_link_libraries(
		Client
		PRIVATE TinyNet
	)
else()
	target_link_libraries(
		Client
		PRIVATE TinyNet 
		PUBLIC ${CMAKE_THREAD_LIBS_INIT}
	)
endif()
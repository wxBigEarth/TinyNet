
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

IF (CMAKE_SYSTEM_NAME MATCHES "Windows")

	# 添加自定义命令
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD

		COMMAND ${CMAKE_COMMAND} -E copy 
			${RUNTIME_OUTPUT_DIRECTORY}/${PROJECT_NAME}.dll
			${INSTALL_SHARED}/${CMAKE_BUILD_TYPE}

		COMMAND ${CMAKE_COMMAND} -E copy
			${ARCHIVE_OUTPUT_DIRECTORY}/${PROJECT_NAME}.lib
			${INSTALL_LIB}/${CMAKE_BUILD_TYPE}

		COMMAND ${CMAKE_COMMAND} -E copy
			${ARCHIVE_OUTPUT_DIRECTORY}/${PROJECT_NAME}.exp
			${INSTALL_LIB}/${CMAKE_BUILD_TYPE}
		
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_CURRENT_LIST_DIR}/Include/
			${INSTALL_ROOT}/include

		COMMENT "Copying library and headers to output directory"
	)

IF (CMAKE_BUILD_TYPE MATCHES "Debug")

	# 添加自定义命令
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD

		COMMAND ${CMAKE_COMMAND} -E copy 
			${RUNTIME_OUTPUT_DIRECTORY}/${PROJECT_NAME}.pdb
			${INSTALL_SHARED}/${CMAKE_BUILD_TYPE}
	)
endif()

endif()
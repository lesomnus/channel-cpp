cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Do not build in-source. Please remove CMakeCache.txt and the CMakeFiles/ directory. Then build out-of-source.")
endif()


project(
	"channel"
		VERSION   0.0.1
		LANGUAGES CXX
)

option(THOR_${PROJECT_NAME}_TESTS "Enable ${PROJECT_NAME} project tests targets." ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)



add_library(
	channel INTERFACE
		include/lesomnus/channel/error.hpp
		include/lesomnus/channel/channel.hpp
		include/lesomnus/channel/select.hpp

		include/lesomnus/channel/unbounded_channel.hpp
		include/lesomnus/channel/bounded_channel.hpp

		include/lesomnus/channel.hpp
)
target_include_directories(
	channel INTERFACE
		include
)
add_library(
	lesomnus::channel
		ALIAS channel
)



if(THOR_${PROJECT_NAME}_TESTS)
	add_subdirectory(tests)
endif()

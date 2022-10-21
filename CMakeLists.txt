cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
	message(FATAL_ERROR "Do not build in-source. Please remove CMakeCache.txt and the CMakeFiles/ directory. Then build out-of-source.")
endif()


project(
	"channel"
		VERSION   0.0.1
		LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

option(THOR_${PROJECT_NAME}_TESTS "Enable ${PROJECT_NAME} project tests targets." ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD          20 )
set(CMAKE_CXX_STANDARD_REQUIRED ON )
set(CMAKE_CXX_EXTENSIONS        OFF)



add_library(
	channel INTERFACE
		include/lesomnus/channel/error.hpp
		include/lesomnus/channel/chan.hpp
		include/lesomnus/channel/select.hpp
		include/lesomnus/channel/channel.hpp

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
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		set(Coverage_LCOV_ARGS --no-external)

		include(CodeCoverage)

		string(REPLACE " " ";" COVERAGE_COMPILER_FLAG_LIST ${COVERAGE_COMPILER_FLAGS})
		target_compile_options(
			channel INTERFACE
				${COVERAGE_COMPILER_FLAG_LIST}
		)
		target_link_libraries(
			channel INTERFACE
				gcov
		)
	
		setup_target_for_coverage_lcov(
			NAME       coverage
			EXECUTABLE ctest --test-dir ${PROJECT_BINARY_DIR}/tests
			EXCLUDE
				"build/*"
				"tests/*"
				"/usr/*"
		)
	endif()

	add_subdirectory(tests)
endif()

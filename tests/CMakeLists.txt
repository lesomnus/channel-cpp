include(FetchContent)

FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(
	Catch2
)

find_package(Threads REQUIRED)



enable_testing()
include(Catch)



macro (LESOMNUS_CHANNEL_TEST NAME)
	add_executable(
		${NAME}_test
			src/${NAME}.cpp
	)
	target_include_directories(
		${NAME}_test PRIVATE
			include
	)
	target_link_libraries(
		${NAME}_test PRIVATE
			Threads::Threads
			Catch2::Catch2WithMain
			lesomnus::channel
	)
	catch_discover_tests(
		${NAME}_test
			EXTRA_ARGS --skip-benchmarks
	)
endmacro (LESOMNUS_CHANNEL_TEST)

LESOMNUS_CHANNEL_TEST(channel)
LESOMNUS_CHANNEL_TEST(select)
LESOMNUS_CHANNEL_TEST(io_bench)

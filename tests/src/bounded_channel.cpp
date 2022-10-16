#include <chrono>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <lesomnus/channel/bounded_channel.hpp>

#include "testing/constants.hpp"
#include "testing/suite/channel.hpp"

struct Initializer {
	template<typename T>
	static std::shared_ptr<lesomnus::channel::chan<T>> make_chan(std::size_t capacity) {
		return std::make_shared<lesomnus::channel::bounded_channel<T>>(capacity);
	}
};

TEST_CASE_METHOD(testing::ChannelTestSuite<Initializer>, "bounded_channel") {
	run_basic();
	run_recv_blocked();
	run_send_blocked();
}

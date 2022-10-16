#include <cstddef>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <lesomnus/channel/unbounded_channel.hpp>

#include "testing/suite/channel.hpp"

struct Initializer {
	template<typename T>
	static std::shared_ptr<lesomnus::channel::chan<T>> make_chan(std::size_t capacity) {
		return std::make_shared<lesomnus::channel::unbounded_channel<T>>();
	}
};

TEST_CASE_METHOD(testing::ChannelTestSuite<Initializer>, "unbounded_channel") {
	run_basic();
	run_recv_blocked();
}

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <lesomnus/channel/chan.hpp>
#include <lesomnus/channel/channel.hpp>

#include "testing/constants.hpp"

template<typename F>
class ChannelTestSuite {
   public:
	template<typename T, std::size_t Cap>
	std::shared_ptr<lesomnus::channel::chan<T>> make_chan() {
		return F::template make_chan<T, Cap>();
	}

	void run_basic() {
		std::stop_source stop_source;
		std::stop_token  stop_token = stop_source.get_token();

		SECTION("send and receive") {
			auto const chan = make_chan<int, 1>();

			int v = 0;
			REQUIRE(chan->send(42));
			REQUIRE(chan->recv(v));
			REQUIRE(42 == v);
		}

		SECTION("operation fails if `stop_token` is stop requested") {
			auto const chan = make_chan<int, 0>();

			stop_source.request_stop();

			int v;
			REQUIRE_FALSE(chan->recv(stop_token, v));
			REQUIRE_FALSE(chan->send(stop_token, 42));
		}

		SECTION("operation fails if channel is closed") {
			auto const chan = make_chan<int, 0>();

			chan->close();

			int v;
			REQUIRE_FALSE(chan->recv(v));
			REQUIRE_FALSE(chan->send(42));
		}
	}

	void run_recv_blocked() {
		std::stop_source stop_source;
		std::stop_token  stop_token = stop_source.get_token();

		SECTION("try receive not blocked even if data unavailable") {
			auto const chan = make_chan<int, 0>();

			int v;
			REQUIRE_FALSE(chan->try_recv(v));
		}

		SECTION("receive blocked until data available") {
			auto const chan = make_chan<int, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				chan->send(42);  // It will throw error if `recv()` not blocked.
			});

			int v = 0;
			chan->recv(v);
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
			REQUIRE(42 == v);
		}

		SECTION("receive fails if operation canceled") {
			auto const chan = make_chan<int, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				stop_source.request_stop();
			});

			int v = 0;
			REQUIRE_FALSE(chan->recv(stop_token, v));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
		}

		SECTION("receive fails if channel closed") {
			auto const chan = make_chan<int, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				chan->close();
			});

			int        v  = 0;
			bool const ok = chan->recv(stop_token, v);
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
			REQUIRE_FALSE(ok);
		}

		SECTION("size is negative if receive hanged") {
			auto const chan = make_chan<int, 0>();

			auto const receiver1 = std::jthread([&](std::stop_token token) {
				int v;
				chan->recv(token, v);
			});

			auto const receiver2 = std::jthread([&](std::stop_token token) {
				int v;
				chan->recv(token, v);
			});

			std::this_thread::sleep_for(testing::ReasonableWaitingTime);
			REQUIRE(-2 == chan->size());
		}
	}

	void run_send_blocked() {
		std::stop_source stop_source;
		std::stop_token  stop_token = stop_source.get_token();

		SECTION("try send not blocked even if buffer unavailable") {
			auto const chan = make_chan<int, 0>();

			REQUIRE_FALSE(chan->try_send(42));
		}

		SECTION("send blocked until buffer available") {
			auto const chan = make_chan<int, 0>();

			int        v;
			auto const t0       = std::chrono::steady_clock::now();
			auto const receiver = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				chan->recv(v);
			});

			chan->send(42);
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
			REQUIRE(42 == v);
		}

		SECTION("send fails if operation canceled") {
			auto const chan = make_chan<int, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				stop_source.request_stop();
			});

			REQUIRE_FALSE(chan->send(stop_token, 42));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
		}

		SECTION("send fails if channel closed") {
			auto const chan = make_chan<int, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				chan->close();
			});

			REQUIRE_FALSE(chan->send(stop_token, 42));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));
		}

		SECTION("size greater than capacity if send hanged") {
			auto const chan = make_chan<int, 0>();

			auto const sender1 = std::jthread([&](std::stop_token token) {
				chan->send(token, 42);
			});

			auto const sender2 = std::jthread([&](std::stop_token token) {
				chan->send(token, 42);
			});

			std::this_thread::sleep_for(testing::ReasonableWaitingTime);
			REQUIRE(2 == chan->size());
		}
	}
};

struct BoundedChanInitializer {
	template<typename T, std::size_t Cap>
	static std::shared_ptr<lesomnus::channel::chan<T>> make_chan() {
		return lesomnus::channel::make_chan<T, Cap>();
	}
};

TEST_CASE_METHOD(ChannelTestSuite<BoundedChanInitializer>, "bounded_channel") {
	run_basic();
	run_recv_blocked();
	run_send_blocked();
}

struct UnboundedChanInitializer {
	template<typename T, std::size_t Cap>
	static std::shared_ptr<lesomnus::channel::chan<T>> make_chan() {
		return lesomnus::channel::make_chan<T, lesomnus::channel::unbounded_capacity>();
	}
};

TEST_CASE_METHOD(ChannelTestSuite<UnboundedChanInitializer>, "unbounded_channel") {
	run_basic();
	run_recv_blocked();
}

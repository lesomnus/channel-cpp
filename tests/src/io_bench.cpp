#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <lesomnus/channel/chan.hpp>
#include <lesomnus/channel/channel.hpp>

TEST_CASE("send") {
	namespace channel = lesomnus::channel;

	auto const send = [](channel::chan<int>& chan, int size) {
		for(int i = 0; i < size; ++i) {
			chan.send(i);
		}
	};

	BENCHMARK("bounded_chanel-10k") {
		channel::bounded_channel<int, 10'000> chan;
		send(chan, 10'000);
	};

	BENCHMARK("bounded_chanel-100k") {
		channel::bounded_channel<int, 100'000> chan;
		send(chan, 100'000);
	};

	BENCHMARK("unbounded_chanel-10k") {
		channel::bounded_channel<int, channel::unbounded_capacity> chan;
		send(chan, 10'000);
	};

	BENCHMARK("unbounded_chanel-100k") {
		channel::bounded_channel<int, channel::unbounded_capacity> chan;
		send(chan, 100'000);
	};
}

TEST_CASE("send_sched") {
	namespace channel = lesomnus::channel;

	auto const send_sched = [](channel::chan<int>& chan, int size) {
		for(int i = 0; i < size; ++i) {
			chan.send_sched(i, [](auto) {});
		}
	};

	BENCHMARK("bounded_channel-10k") {
		channel::bounded_channel<int, 0> chan;
		send_sched(static_cast<channel::chan<int>&>(chan), 10'000);
	};

	BENCHMARK("bounded_channel-100k") {
		channel::bounded_channel<int, 0> chan;
		send_sched(chan, 100'000);
	};
}

TEST_CASE("recv_sched") {
	namespace channel = lesomnus::channel;

	auto const recv_sched = [](channel::chan<int>& chan, int size) {
		for(int i = 0; i < size; ++i) {
			chan.recv_sched([](auto, auto) {});
		}
	};

	BENCHMARK("bounded_channel_0-10k") {
		channel::bounded_channel<int, 0> chan;
		recv_sched(chan, 10'000);
	};

	BENCHMARK("bounded_channel_0-100k") {
		channel::bounded_channel<int, 0> chan;
		recv_sched(chan, 100'000);
	};

	BENCHMARK("bounded_channel-10k") {
		channel::bounded_channel<int, 1> chan;
		recv_sched(chan, 10'000);
	};

	BENCHMARK("bounded_channel-100k") {
		channel::bounded_channel<int, 1> chan;
		recv_sched(chan, 100'000);
	};

	BENCHMARK("unbounded_channel-10k") {
		channel::bounded_channel<int, channel::unbounded_capacity> chan;
		recv_sched(chan, 10'000);
	};

	BENCHMARK("unbounded_channel-100k") {
		channel::bounded_channel<int, channel::unbounded_capacity> chan;
		recv_sched(chan, 100'000);
	};
}

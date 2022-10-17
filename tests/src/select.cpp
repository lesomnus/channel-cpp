#include <chrono>
#include <string>
#include <thread>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <lesomnus/channel.hpp>

#include "testing/constants.hpp"

TEST_CASE("select") {
	using namespace lesomnus::channel;

	SECTION("cancels un-settled operations if one of operation is settled") {
		SECTION("by immediate operation") {
			auto chan1 = unbounded_channel<int>();
			auto chan2 = unbounded_channel<std::string>();

			std::string sent;
			select(
			    recv(chan1),
			    send(chan2, "foo", [&sent](bool) { sent = "foo"; }),
			    send(chan2, "bar", [&sent](bool) { sent = "bar"; }));

			REQUIRE(0 == chan1.size());  // It will be -1 if it is not canceled.
			REQUIRE(1 == chan2.size());  // It will be  2 if it is not canceled.
			REQUIRE("foo" == sent);

			chan2.send("baz");

			std::string received;

			chan2.recv(received);
			REQUIRE("foo" == received);

			chan2.recv(received);
			REQUIRE("baz" == received);
		}

		SECTION("by send") {
			auto chan1 = unbounded_channel<int>();
			auto chan2 = unbounded_channel<std::string>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const sender = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);
				chan2.send("foo");
			});

			int i = 0;
			select(
			    recv(chan1),
			    recv(chan2, [&i](bool, std::string&&) { i = 1; }),
			    recv(chan2, [&i](bool, std::string&&) { i = 2; }));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));

			REQUIRE(0 == chan1.size());  // It will be -1 if it is not canceled.
			REQUIRE(0 == chan2.size());  // It will be -1 if it is not canceled.

			// `i` can be 2 if `chan2.send` is invoked
			// after first `chan2.recv_sched` and before second `cha2.recv_sched`.
			REQUIRE(0 != i);

			chan2.send("bar");

			std::string received;
			chan2.recv(received);
			REQUIRE("bar" == received);
		}

		SECTION("by receive") {
			auto chan1 = bounded_channel<int, 0>();
			auto chan2 = bounded_channel<std::string, 0>();

			auto const  t0 = std::chrono::steady_clock::now();
			std::string received;
			auto const  receiver = std::jthread([&] {
                std::this_thread::sleep_for(testing::ReasonableWaitingTime);

                chan2.recv(received);
            });

			std::string sent;
			select(
			    send(chan1, 42),
			    send(chan2, "foo", [&sent](bool) { sent = "foo"; }),
			    send(chan2, "bar", [&sent](bool) { sent = "bar"; }));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));

			REQUIRE(0 == chan1.size());  // It will be -1 if it is not canceled.
			REQUIRE(0 == chan2.size());  // It will be  1 if it is not canceled.

			// `i` can be 2 if `chan2.send` is invoked
			// after first `chan2.recv_sched` and before second `cha2.recv_sched`.
			REQUIRE("" != sent);
			REQUIRE(sent == received);
		}

		SECTION("by close") {
			auto chan1 = bounded_channel<int, 0>();
			auto chan2 = bounded_channel<std::string, 0>();

			auto const t0     = std::chrono::steady_clock::now();
			auto const closer = std::jthread([&] {
				std::this_thread::sleep_for(testing::ReasonableWaitingTime);

				chan2.close();
			});

			int i = 0;
			select(
			    recv(chan1),
			    send(chan2, "foo", [&i](bool) { ++i; }),
			    send(chan2, "bar", [&i](bool) { ++i; }));
			auto const t1 = std::chrono::steady_clock::now();

			REQUIRE(testing::ReasonableWaitingTime <= (t1 - t0));

			REQUIRE(0 == chan1.size());  // It will be -1 if it is not canceled.
			REQUIRE(0 == chan2.size());  // It will be  1 if it is not canceled.
			REQUIRE(1 == i);             // It will be  2 if it is not canceled.
		}
	}
}

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <utility>

#include "lesomnus/channel/error.hpp"

namespace lesomnus {
namespace channel {

namespace detail {

class chan_base {
   public:
	virtual std::intmax_t size() const = 0;

	virtual std::size_t capacity() const = 0;

	virtual void close() = 0;
};

}  // namespace detail

template<typename T>
class receiver: public virtual detail::chan_base {
   public:
	virtual void try_recv(T& value, std::error_code& ec) = 0;

	bool try_recv(T& value) {
		std::error_code ec;
		try_recv(value, ec);
		return ec == channel_errc::ok;
	}

	virtual void recv(std::stop_token token, T& value, std::error_code& ec) = 0;

	bool recv(std::stop_token token, T& value) {
		std::error_code ec;
		recv(token, value, ec);
		return ec == channel_errc::ok;
	}

	bool recv(T& value) {
		return recv(std::stop_token{}, value);
	}

	virtual void recv_sched(std::function<bool()> need_abort, std::function<void(bool, T&&)> on_settled) = 0;

	void recv_sched(std::stop_token token, std::function<void(bool, T&&)> on_settled) {
		if(token.stop_requested()) {
			return;
		}

		recv_sched([token] { return token.stop_requested(); }, std::move(on_settled));
	}

	void recv_sched(std::function<void(bool, T&&)> on_settled) {
		recv_sched([] { return false; }, std::move(on_settled));
	}
};

template<typename T>
class sender: public virtual detail::chan_base {
   public:
	virtual void try_send(T const& value, std::error_code& ec) = 0;

	virtual void try_send(T&& value, std::error_code& ec) = 0;

	bool try_send(T const& value) {
		std::error_code ec;
		try_send(value, ec);
		return ec == channel_errc::ok;
	}

	bool try_send(T&& value) {
		std::error_code ec;
		try_send(std::move(value), ec);
		return ec == channel_errc::ok;
	}

	virtual void send(std::stop_token token, T const& value, std::error_code& ec) = 0;

	virtual void send(std::stop_token token, T&& value, std::error_code& ec) = 0;

	bool send(std::stop_token token, T const& value) {
		std::error_code ec;
		send(token, value, ec);
		return ec == channel_errc::ok;
	}

	bool send(std::stop_token token, T&& value) {
		std::error_code ec;
		send(token, std::move(value), ec);
		return ec == channel_errc::ok;
	}

	bool send(T const& value) {
		return send(std::stop_token{}, value);
	}

	bool send(T&& value) {
		return send(std::stop_token{}, std::move(value));
	}

	virtual void send_sched(T const& value, std::function<bool()> need_abort, std::function<void(bool)> on_settled) = 0;

	virtual void send_sched(T&& value, std::function<bool()> need_abort, std::function<void(bool)> on_settled) = 0;

	void send_sched(std::stop_token token, T const& value, std::function<void(bool, T&&)> on_settled) {
		if(token.stop_requested()) {
			return;
		}

		send_sched(
		    value, [token] { return token.stop_requested(); }, std::move(on_settled));
	}

	void send_sched(std::stop_token token, T&& value, std::function<void(bool)> on_settled) {
		if(token.stop_requested()) {
			return;
		}

		send_sched(
		    std::move(value), [token] { return token.stop_requested(); }, std::move(on_settled));
	}

	void send_sched(T const& value, std::function<void(bool)> on_settled) {
		send_sched(
		    value, [] { return false; }, std::move(on_settled));
	}

	void send_sched(T&& value, std::function<void(bool)> on_settled) {
		send_sched(
		    std::move(value), [] { return false; }, std::move(on_settled));
	}
};

template<typename T>
class chan
    : public receiver<T>
    , public sender<T> { };

}  // namespace channel
}  // namespace lesomnus

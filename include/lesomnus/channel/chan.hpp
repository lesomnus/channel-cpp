#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <stop_token>
#include <utility>

#include "lesomnus/channel/error.hpp"

namespace lesomnus {
namespace channel {

namespace detail {

class chan_base {
   public:
	/**
	 * @brief Returns the number of element that currently held.
	 * 
	 * It can be negative or greater than its capacity if receiver or sender hangs.
	 */
	virtual std::ptrdiff_t size() const = 0;

	/**
	 * @brief Returns the number of elements that can be held.
	 */
	virtual std::size_t capacity() const = 0;

	/**
	 * @brief Closes the channel and release holding operations.
	 * 
	 */
	virtual void close() = 0;
};

}  // namespace detail

template<typename T>
class receiver: public virtual detail::chan_base {
   public:
	/**
	 * @brief Extracts the first element from the buffer.
	 * 
	 * Get the sender's value if capacity is 0 and there are hanging senders.
	 * Fails if there is no element in the buffer or no hanging sender.
	 * 
	 * @param[out] value Where the received values will be assigned.
	 * @param[out] ec Error report.
	 */
	virtual void try_recv(T& value, std::error_code& ec) = 0;

	/**
	 * @brief Extracts the first element from the buffer.
	 * 
	 * Get the sender's value if capacity is 0 and there are hanging senders.
	 * 
	 * @param[out] value Where the received values will be assigned.
	 * @return False if no value is received.
	 */
	bool try_recv(T& value) {
		std::error_code ec;
		try_recv(value, ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Extracts the first element from the buffer.
	 * 
	 * Unlike \ref try_send, it blocked until the element is received.
	 * Fails if \p token is stop requested or the channel closed.
	 * 
	 * @param[in]  token Interrupt register.
	 * @param[out] value Where the received values will be assigned.
	 * @param[out] ec Error report.
	 */
	virtual void recv(std::stop_token token, T& value, std::error_code& ec) = 0;

	/**
	 * @brief Extracts the first element from the buffer.
	 * 
	 * Unlike \ref try_send, it blocked until the element is received.
	 * 
	 * @param[in]  token Interrupt register.
	 * @param[out] value Where the received values will be assigned.
	 * @return False if no value is received.
	 */
	bool recv(std::stop_token token, T& value) {
		std::error_code ec;
		recv(token, value, ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Extracts the first element from the buffer.
	 * 
	 * Unlike \ref try_send, it blocked until the element is received.
	 * 
	 * @param[out] value Where the received values will be assigned.
	 * @return False if no value is received.
	 */
	bool recv(T& value) {
		return recv(std::stop_token{}, value);
	}

	/**
	 * @brief Registers callback function that will be called when the value is received.
	 * 
	 * If the element is available or there are hanging senders, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p need_abort is called to see if the scheduled
	 * operation is steel valid. If \p need_abort returns false, the callback is removed.
	 * 
	 * @param need_abort Validator.
	 * @param on_settled Callback function.
	 */
	virtual void recv_sched(std::function<bool()> need_abort, std::function<void(bool, T&&)> on_settled) = 0;

	/**
	 * @brief Registers callback function that will be called when the value is received.
	 * 
	 * If the element is available or there are hanging senders, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p token is checked to see if the scheduled
	 * operation is steel valid. If \p token is stop requested, the callback is removed.
	 * 
	 * @param token Validator.
	 * @param on_settled Callback function.
	 */
	void recv_sched(std::stop_token token, std::function<void(bool, T&&)> on_settled) {
		if(token.stop_requested()) {
			return;
		}

		recv_sched([token] { return token.stop_requested(); }, std::move(on_settled));
	}

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the element is available, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * @param on_settled Callback function.
	 */
	void recv_sched(std::function<void(bool, T&&)> on_settled) {
		recv_sched([] { return false; }, std::move(on_settled));
	}
};

template<typename T>
class sender: public virtual detail::chan_base {
   public:
	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Fails if there is no space in the buffer or if there is no hanging receiver.
	 * 
	 * @param[in]  value Value to send.
	 * @param[out] ec Error report.
	 */
	virtual void try_send(T const& value, std::error_code& ec) = 0;

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Fails if there is no space in the buffer or if there is no hanging receiver.
	 * 
	 * @param[in]  value Value to send.
	 * @param[out] ec Error report.
	 */
	virtual void try_send(T&& value, std::error_code& ec) = 0;

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * 
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool try_send(T const& value) {
		std::error_code ec;
		try_send(value, ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * 
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool try_send(T&& value) {
		std::error_code ec;
		try_send(std::move(value), ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param[in]  token Interrupt register.
	 * @param[in]  value Value to send.
	 * @param[out] ec Error report.
	 */
	virtual void send(std::stop_token token, T const& value, std::error_code& ec) = 0;

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param[in]  token Interrupt register.
	 * @param[in]  value Value to send.
	 * @param[out] ec Error report.
	 */
	virtual void send(std::stop_token token, T&& value, std::error_code& ec) = 0;

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param token Interrupt register.
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool send(std::stop_token token, T const& value) {
		std::error_code ec;
		send(token, value, ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param token Interrupt register.
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool send(std::stop_token token, T&& value) {
		std::error_code ec;
		send(token, std::move(value), ec);
		return ec == channel_errc::ok;
	}

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool send(T const& value) {
		return send(std::stop_token{}, value);
	}

	/**
	 * @brief Appends the value to the end of the buffer.
	 * 
	 * Forwards the value to receiver if capacity is 0 and there are hanging receivers.
	 * Unlike \ref try_send, it blocked until the value is sent.
	 * 
	 * @param value Value to send.
	 * @return False if no value is sent.
	 */
	bool send(T&& value) {
		return send(std::stop_token{}, std::move(value));
	}

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p need_abort is called to see if the scheduled
	 * operation is steel valid. If \p need_abort returns false, the callback is removed.
	 * 
	 * @param value Value to send.
	 * @param need_abort Validator.
	 * @param on_settled Callback function.
	 */
	virtual void send_sched(T const& value, std::function<bool()> need_abort, std::function<void(bool)> on_settled) = 0;

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p need_abort is called to see if the scheduled
	 * operation is steel valid. If \p need_abort returns false, the callback is removed.
	 * 
	 * @param value Value to send.
	 * @param need_abort Validator.
	 * @param on_settled Callback function.
	 */
	virtual void send_sched(T&& value, std::function<bool()> need_abort, std::function<void(bool)> on_settled) = 0;

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p token is checked to see if the scheduled
	 * operation is steel valid. If \p token is stop requested, the callback is removed.
	 * 
	 * @param value Value to send.
	 * @param token Validator.
	 * @param on_settled Callback function.
	 */
	void send_sched(std::stop_token token, T const& value, std::function<void(bool, T&&)> on_settled) {
		if(token.stop_requested()) [[unlikely]] {
			return;
		}

		send_sched(
		    value, [token] { return token.stop_requested(); }, std::move(on_settled));
	}

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * When accessing the buffer, \p token is checked to see if the scheduled
	 * operation is steel valid. If \p token is stop requested, the callback is removed.
	 * 
	 * @param value Value to send.
	 * @param token Validator.
	 * @param on_settled Callback function.
	 */
	void send_sched(std::stop_token token, T&& value, std::function<void(bool)> on_settled) {
		if(token.stop_requested()) [[unlikely]] {
			return;
		}

		send_sched(
		    std::move(value), [token] { return token.stop_requested(); }, std::move(on_settled));
	}

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * @param value Value to send.
	 * @param on_settled Callback function.
	 */
	void send_sched(T const& value, std::function<void(bool)> on_settled) {
		send_sched(
		    value, [] { return false; }, std::move(on_settled));
	}

	/**
	 * @brief Registers callback function that will be called when the value is sent.
	 * 
	 * If the buffer is not full or there are hanging receivers, the callback function is called immediately,
	 * otherwise the callback function is called on the sender's thread.
	 * 
	 * @param value Value to send.
	 * @param on_settled Callback function.
	 */
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

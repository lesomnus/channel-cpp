#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <system_error>
#include <utility>

#include "lesomnus/channel/channel.hpp"
#include "lesomnus/channel/detail/task.hpp"
#include "lesomnus/channel/error.hpp"

namespace lesomnus {
namespace channel {

template<typename T>
class unbounded_channel: public chan<T> {
   public:
	using recv_task = detail::task<std::function<void(bool, T&&)>>;

	using chan<T>::recv;
	using chan<T>::send;

	std::intmax_t size() const override {
		std::scoped_lock l(mutex_);

		// Garbage collection of aborted tasks.
		while(!hanged_recv_tasks.empty()) {
			if(!hanged_recv_tasks.front().need_abort()) {
				break;
			}

			hanged_recv_tasks.pop();
		}

		return buffer_.size() - hanged_recv_tasks.size();
	}

	constexpr std::size_t capacity() const noexcept override {
		return std::numeric_limits<std::size_t>::max();
	}

	void close() override {
		std::scoped_lock l(mutex_);
		is_closed_ = true;

		T v;
		while(!hanged_recv_tasks.empty()) {
			auto& task = hanged_recv_tasks.front();
			if(!task.need_abort()) {
				task.execute(false, std::move(v));
			}
			hanged_recv_tasks.pop();
		}
	}

	void try_recv(T& value, std::error_code& ec) override {
		std::scoped_lock l(mutex_);

		if(is_closed_) {
			ec = channel_errc::closed;
		} else if(try_recv_(value)) {
			ec = channel_errc::ok;
		} else {
			ec = channel_errc::exhausted;
		}
	}

	void recv(std::stop_token token, T& value, std::error_code& ec) override {
		std::unique_lock l(mutex_);

		if(token.stop_requested()) [[unlikely]] {
			ec = channel_errc::canceled;
			return;
		}

		if(is_closed_) [[unlikely]] {
			ec = channel_errc::closed;
			return;
		}

		if(try_recv_(value)) {
			ec = channel_errc::ok;
			return;
		}

		std::mutex done;
		done.lock();

		std::stop_source task_stop_source;
		std::stop_token  task_token = task_stop_source.get_token();

		std::stop_callback on_cancel(token, [this, &ec, &done, &task_stop_source, task_token] {
			std::unique_lock l(mutex_);

			// Task already processed so captured references are invalid.
			if(task_token.stop_requested()) {
				return;
			}

			ec = channel_errc::canceled;

			done.unlock();
			task_stop_source.request_stop();
		});

		hanged_recv_tasks.emplace(
		    [task_token] { return task_token.stop_requested(); },
		    [&value, &ec, &done, &task_stop_source](bool ok, T&& src) {
			    // Prevent on_cancel to be proceed.
			    task_stop_source.request_stop();

			    if(ok) [[likely]] {
				    value = std::move(src);
				    ec    = channel_errc::ok;
			    } else {
				    ec = channel_errc::closed;
			    }

			    done.unlock();
			    return true;
		    });

		l.unlock();
		std::scoped_lock wait(done);
	}

	void recv_sched(
	    std::function<bool()>          need_abort,
	    std::function<void(bool, T&&)> on_settled) override {
		std::unique_lock l(mutex_);

		T value;

		if(is_closed_) [[unlikely]] {
			on_settled(false, std::move(value));
			return;
		}

		if(try_recv_(value)) {
			on_settled(true, std::move(value));
			return;
		}

		hanged_recv_tasks.emplace(
		    std::move(need_abort),
		    std::move(on_settled));
	}

	void try_send(T const& value, std::error_code& ec) override {
		return try_send_(value, ec);
	}

	void try_send(T&& value, std::error_code& ec) override {
		return try_send_(std::move(value), ec);
	}

	void send(std::stop_token token, T const& value, std::error_code& ec) override {
		return send_(token, value, ec);
	}

	void send(std::stop_token token, T&& value, std::error_code& ec) override {
		return send_(token, std::move(value), ec);
	}

	void send_sched(
	    T const&                  value,
	    std::function<bool()>     need_abort,
	    std::function<void(bool)> on_settled) override {
		send_sched_(value, std::move(need_abort), std::move(on_settled));
	}

	void send_sched(
	    T&&                       value,
	    std::function<bool()>     need_abort,
	    std::function<void(bool)> on_settled) override {
		send_sched_(std::move(value), std::move(need_abort), std::move(on_settled));
	}

   private:
	bool try_recv_(T& value) {
		if(buffer_.empty()) {
			return false;
		}

		value = std::move(buffer_.front());

		buffer_.pop();
		return true;
	}

	// Assume channel is not closed.
	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	bool try_send_(U&& value) {
		assert(not is_closed_);

		while(!hanged_recv_tasks.empty()) {
			assert(buffer_.empty());

			auto const& task = hanged_recv_tasks.front();
			if(task.need_abort()) {
				hanged_recv_tasks.pop();
				continue;
			}

			T v = std::forward<U>(value);
			task.execute(true, std::move(v));

			hanged_recv_tasks.pop();
			return true;
		}

		buffer_.emplace(std::forward<U>(value));
		return true;
	}

	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	void try_send_(U&& value, std::error_code& ec) {
		std::scoped_lock l(mutex_);

		if(is_closed_) [[unlikely]] {
			ec = channel_errc::closed;
		} else {
			try_send_(std::forward<U>(value));
			ec = channel_errc::ok;
		}
	}

	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	void send_(std::stop_token token, U&& value, std::error_code& ec) {
		std::scoped_lock l(mutex_);

		if(token.stop_requested()) [[unlikely]] {
			ec = channel_errc::canceled;
		} else if(is_closed_) [[unlikely]] {
			ec = channel_errc::closed;
		} else {
			try_send_(std::forward<U>(value));
			ec = channel_errc::ok;
		}
	}

	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	void send_sched_(
	    U&&                       value,
	    std::function<bool()>     need_abort,
	    std::function<void(bool)> on_settled) {
		std::scoped_lock l(mutex_);

		if(is_closed_) [[unlikely]] {
			on_settled(false);
			return;
		}

		bool const ok = try_send_(std::forward<U>(value));
		on_settled(ok);
	}

	mutable std::mutex mutex_;

	bool          is_closed_ = false;
	std::queue<T> buffer_;

	// This is mutable since aborted tasks are garbage collected while `size() const`.
	mutable std::queue<recv_task> hanged_recv_tasks;
};

}  // namespace channel
}  // namespace lesomnus

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
			if(!hanged_recv_tasks.front().is_aborted()) {
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
			if(!task.is_aborted()) {
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

		hanged_recv_tasks.emplace(recv_task{
		    task_token,
		    [] { return false; },
		    [&value, &ec, &done, &task_stop_source](bool ok, T&& src) {
			    // `mutex_` must be locked invoke this before.
			    // It must not be invoked if `task_token` is expired.

			    // Prevent on_cancel to be proceed.
			    task_stop_source.request_stop();

			    if(ok) [[likely]] {
				    value = std::move(src);
				    ec    = channel_errc::ok;
			    } else {
				    ec = channel_errc::closed;
			    }

			    done.unlock();
		    },
		});

		l.unlock();
		std::scoped_lock wait(done);
	}

	void recv_sched(
	    std::stop_token                       token,
	    std::function<bool()> const&          need_abort,
	    std::function<void(bool, T&&)> const& on_settle) override {
		std::unique_lock l(mutex_);

		if(token.stop_requested()) [[unlikely]] {
			return;
		}

		T value;

		if(is_closed_) [[unlikely]] {
			on_settle(false, std::move(value));
			return;
		}

		if(try_recv_(value)) {
			on_settle(true, std::move(value));
			return;
		}

		hanged_recv_tasks.emplace(recv_task{
		    token,
		    need_abort,
		    [&on_settle](bool ok, T&& src) {
			    on_settle(ok, std::move(src));
		    },
		});
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
	    std::stop_token token, T const& value,
	    std::function<bool()> const&     need_abort,
	    std::function<void(bool)> const& on_settle) override {
		send_sched_(token, value, need_abort, on_settle);
	}

	void send_sched(
	    std::stop_token token, T&& value,
	    std::function<bool()> const&     need_abort,
	    std::function<void(bool)> const& on_settle) override {
		send_sched_(token, std::move(value), need_abort, on_settle);
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

			auto const& task       = hanged_recv_tasks.front();
			bool const  is_aborted = task.is_aborted();

			if(is_aborted) {
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
	    std::stop_token token, U&& value,
	    std::function<bool()> const&     need_abort,
	    std::function<void(bool)> const& on_settle) {
		std::scoped_lock l(mutex_);

		if(token.stop_requested()) [[unlikely]] {
			return;
		}

		bool const ok = try_send_(std::forward<U>(value));
		on_settle(ok);
	}

	mutable std::mutex mutex_;

	bool          is_closed_ = false;
	std::queue<T> buffer_;

	// This is mutable since aborted tasks are garbage collected while `size() const`.
	mutable std::queue<recv_task> hanged_recv_tasks;
};

}  // namespace channel
}  // namespace lesomnus

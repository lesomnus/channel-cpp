#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <system_error>
#include <type_traits>
#include <utility>

#include "lesomnus/channel/channel.hpp"
#include "lesomnus/channel/detail/task.hpp"
#include "lesomnus/channel/error.hpp"

namespace lesomnus {
namespace channel {

inline constexpr std::size_t unbounded_capacity = -1;

template<typename T, std::size_t Cap = 0>
class bounded_channel: public chan<T> {
   public:
	using send_task = detail::task<std::function<void(bool, T&)>>;
	using recv_task = detail::task<std::function<void(bool, T&&)>>;

	using chan<T>::recv;
	using chan<T>::send;

	std::intmax_t size() const override {
		std::scoped_lock l(mutex_);

		T v;
		while(!hanged_recv_tasks.empty()) {
			if(!hanged_recv_tasks.front().need_abort()) {
				break;
			}

			hanged_recv_tasks.pop();
		}

		if constexpr(Cap != unbounded_capacity) {
			while(!hanged_send_tasks.empty()) {
				if(!hanged_send_tasks.front().need_abort()) {
					break;
				}

				hanged_send_tasks.pop();
			}
		}

		return buffer_.size() + hanged_send_tasks.size() - hanged_recv_tasks.size();
	}

	constexpr std::size_t capacity() const noexcept override {
		return Cap;
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

		if constexpr(Cap != unbounded_capacity) {
			while(!hanged_send_tasks.empty()) {
				auto& task = hanged_send_tasks.front();
				if(!task.need_abort()) {
					task.execute(false, v);
				}
				hanged_send_tasks.pop();
			}
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
		    [task_token] { return task_token.stop_requested(); },
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
		assert(not is_closed_);

		if constexpr(Cap != 0) {
			if(!buffer_.empty()) {
				assert(Cap > 0);

				value = std::move(buffer_.front());
				buffer_.pop();

				if constexpr(Cap != unbounded_capacity) {
					while(!hanged_send_tasks.empty()) {
						assert(buffer_.size() < Cap);

						auto const& task = hanged_send_tasks.front();
						if(task.need_abort()) {
							hanged_send_tasks.pop();
							continue;
						}

						T& v = buffer_.emplace();
						task.execute(true, v);
					}
				}

				return true;
			}
		}

		if constexpr(Cap != unbounded_capacity) {
			if(!hanged_send_tasks.empty()) {
				assert(Cap == 0);

				while(!hanged_send_tasks.empty()) {
					assert(buffer_.size() >= Cap);

					auto const& task = hanged_send_tasks.front();
					if(task.need_abort()) {
						hanged_send_tasks.pop();
						continue;
					}

					task.execute(true, value);
					hanged_send_tasks.pop();
					return true;
				}
			}
		}

		return false;
	}

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

		if constexpr(Cap == 0) {
			return false;
		}

		if constexpr(Cap != unbounded_capacity) {
			if(buffer_.size() >= Cap) {
				return false;
			}
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
			if constexpr(Cap == unbounded_capacity) {
				try_send_(std::forward<U>(value));
				ec = channel_errc::ok;
			} else {
				if(try_send_(std::forward<U>(value))) {
					ec = channel_errc::ok;
				} else {
					ec = channel_errc::exhausted;
				}
			}
		}
	}

	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	void send_(std::stop_token token, U&& value, std::error_code& ec) {
		std::unique_lock l(mutex_);

		if(token.stop_requested()) [[unlikely]] {
			ec = channel_errc::canceled;
			return;
		}

		if(is_closed_) [[unlikely]] {
			ec = channel_errc::closed;
			return;
		}

		if constexpr(Cap == unbounded_capacity) {
			try_send_(std::forward<U>(value));
			ec = channel_errc::ok;
			return;
		} else {
			if(try_send_(std::forward<U>(value))) {
				ec = channel_errc::ok;
				return;
			}
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

		hanged_send_tasks.emplace(send_task{
		    [task_token] { return task_token.stop_requested(); },
		    [&value, &ec, &done, &task_stop_source](bool ok, T& dst) {
			    // `mutex_` must be locked invoke this before.
			    // It must not be invoked if `task_token` is expired.

			    // Prevent on_cancel to be proceed.
			    task_stop_source.request_stop();

			    if(ok) [[likely]] {
				    dst = std::move(value);
				    ec  = channel_errc::ok;
			    } else {
				    ec = channel_errc::closed;
			    }

			    done.unlock();
		    },
		});

		l.unlock();
		std::scoped_lock wait(done);
	}

	template<typename U>
	requires std::same_as<std::remove_cvref_t<U>, T>
	void send_sched_(
	    U&&                       value,
	    std::function<bool()>     need_abort,
	    std::function<void(bool)> on_settled) {
		std::unique_lock l(mutex_);

		if(is_closed_) [[unlikely]] {
			on_settled(false);
			return;
		}

		if constexpr(Cap == unbounded_capacity) {
			try_send_(std::forward<U>(value));
			on_settled(true);
			return;
		} else if constexpr(Cap == 0) {
			if(try_send_(std::forward<U>(value))) {
				on_settled(true);
				return;
			}
		}

		hanged_send_tasks.emplace(send_task{
		    std::move(need_abort),
		    [&value, f = std::move(on_settled)](bool ok, T& dst) {
			    if(ok) {
				    dst = std::forward<U>(value);
			    }

			    f(ok);
		    },
		});
	}

	mutable std::mutex mutex_;

	bool          is_closed_ = false;
	std::queue<T> buffer_;

	mutable std::queue<recv_task> hanged_recv_tasks;
	mutable std::queue<send_task> hanged_send_tasks;
};

template<typename T>
using unbounded_channel = bounded_channel<T, unbounded_capacity>;

template<typename T, std::size_t Cap = 0>
std::shared_ptr<chan<T>> make_chan() {
	return std::make_shared<bounded_channel<T, Cap>>();
}

}  // namespace channel
}  // namespace lesomnus

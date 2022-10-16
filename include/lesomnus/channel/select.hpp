#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <system_error>
#include <type_traits>

#include "lesomnus/channel/channel.hpp"

namespace lesomnus {
namespace channel {

namespace detail {

class op {
   public:
	virtual bool try_execute() = 0;

	virtual void schedule(std::stop_token token, std::function<bool()> const& need_abort) = 0;
};

}  // namespace detail

template<typename T>
class recv: public detail::op {
   public:
	template<typename F = std::function<void(bool, T&&)>>
	recv(
	    receiver<T>& chan, F&& on_settle = [](bool, T&&) {})
	    : chan_(chan)
	    , on_settle_(std::forward<F>(on_settle)) { }

	bool try_execute() override {
		T value;

		std::error_code ec;
		chan_.try_recv(value, ec);

		if(ec == channel_errc::exhausted) {
			return false;
		}

		on_settle_(ec == channel_errc::ok, std::move(value));

		return true;
	}

	void schedule(std::stop_token token, std::function<bool()> const& need_abort) override {
		chan_.recv_sched(token, need_abort, on_settle_);
	}

   private:
	receiver<T>& chan_;

	std::function<void(bool, T&&)> on_settle_;
};

template<typename T, typename I>
class send: public detail::op {
   public:
	send(
	    sender<T>& chan, I&& value, std::function<void(bool)> const& on_settle = [](bool) {})
	    : chan_(chan)
	    , value_(std::forward<I>(value))
	    , on_settle_(on_settle) { }

	bool try_execute() override {
		std::error_code ec;
		chan_.try_send(std::forward<I>(value_), ec);

		if(ec == channel_errc::exhausted) {
			return false;
		}

		on_settle_(ec == channel_errc::ok);

		return true;
	}

	void schedule(std::stop_token token, std::function<bool()> const& need_abort) override {
		chan_.send_sched(token, std::forward<I>(value_), need_abort, on_settle_);
	}

   private:
	sender<T>& chan_;
	I          value_;

	std::function<void(bool)> const& on_settle_;
};

template<class T>
send(sender<T>& chan, char const* value, std::function<void(bool)> const& on_settle) -> send<T, std::string>;

template<typename... Ops>
requires std::conjunction_v<std::is_base_of<detail::op, Ops>...>
void select(std::stop_token token, Ops... ops, std::function<void()> const& fallback) {
	{
		bool is_executed = false;
		([&] {
			if(is_executed) {
				return;
			}

			detail::op& op = ops;
			is_executed    = op.try_execute();
		}(),
		 ...);

		if(is_executed) {
			return;
		}
	}

	if(fallback) {
		fallback();
		return;
	}

	auto schedule = std::make_shared<std::mutex>();
	schedule->lock();

	std::mutex done;
	done.lock();

	std::stop_source op_stop_source;

	auto const cancel = [&done, &op_stop_source] {
		op_stop_source.request_stop();

		done.unlock();
	};

	// TODO: merge with need_abort?
	std::stop_callback on_cancel(token, [schedule, &cancel] {
		std::scoped_lock l(*schedule);
		if(schedule.unique()) {
			return;
		}

		cancel();
	});

	std::function<bool()> const need_abort = [schedule, &cancel]() {
		std::scoped_lock l(*schedule);
		if(schedule.unique()) {
			return true;
		}

		cancel();

		return false;
	};

	([&] {
		detail::op& op = ops;

		op.schedule(op_stop_source.get_token(), need_abort);
	}(),
	 ...);

	schedule->unlock();
	std::scoped_lock wait(done);
}

template<typename... Ops>
requires std::conjunction_v<std::is_base_of<detail::op, Ops>...>
void select(Ops&&... ops) {
	select<Ops...>(std::stop_token{}, std::forward<Ops>(ops)..., nullptr);
}

template<typename... Ops>
requires std::conjunction_v<std::is_base_of<detail::op, Ops>...>
void select(std::stop_token token, Ops&&... ops) {
	select<Ops...>(token, std::forward<Ops>(ops)..., nullptr);
}

template<typename... Ops>
requires std::conjunction_v<std::is_base_of<detail::op, Ops>...>
void select(Ops&&... ops, std::function<void()> const& fallback) {
	select<Ops...>(std::stop_token{}, std::forward<Ops>(ops)..., fallback);
}

}  // namespace channel
}  // namespace lesomnus

#pragma once

#include <functional>
#include <stop_token>
#include <utility>

namespace lesomnus {
namespace channel {
namespace detail {

template<typename F>
struct task {
	bool is_aborted() const {
		return token.stop_requested() || need_abort();
	}

	template<typename... Args>
	void execute(Args&&... args) const {
		func(std::forward<Args>(args)...);
	}

	std::stop_token              token;
	std::function<bool()> const& need_abort;

	F func;
};

}  // namespace detail
}  // namespace channel
}  // namespace lesomnus

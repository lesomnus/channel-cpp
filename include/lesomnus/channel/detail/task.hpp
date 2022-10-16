#pragma once

#include <functional>
#include <stop_token>
#include <utility>

namespace lesomnus {
namespace channel {
namespace detail {

template<typename F>
struct task {
	std::function<bool()> need_abort;

	F execute;
};

}  // namespace detail
}  // namespace channel
}  // namespace lesomnus

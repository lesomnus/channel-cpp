#pragma once

#include <string>
#include <system_error>

namespace lesomnus {
namespace channel {

enum class channel_errc {
	ok        = 0,
	exhausted = 1,
	closed    = 2,
	canceled  = 3,
};

namespace detail {

class channel_category: public std::error_category {
   public:
	[[nodiscard]] char const* name() const noexcept final {
		return "channel_error";
	}

	[[nodiscard]] std::string message(int c) const final {
		switch(static_cast<channel_errc>(c)) {
		case channel_errc::ok:
			return "channel operation successful";
		case channel_errc::exhausted:
			return "channel resource exhausted";
		case channel_errc::closed:
			return "closed channel";
		case channel_errc::canceled:
			return "channel operation canceled";

		default:
			return "unknown";
		}
	}

	// std::error_condition default_error_condition(int c) const noexcept override final {
	// 	switch(static_cast<channel_errc>(c)) {
	// 	case channel_errc::exhausted:
	// 		return make_error_condition(std::errc::resource_unavailable_try_again);
	// 	case channel_errc::closed:
	// 		return make_error_condition(std::errc::connection_reset);
	// 	case channel_errc::canceled:
	// 		return make_error_condition(std::errc::operation_canceled);
	// 	default:
	// 		return std::error_condition(c, *this);
	// 	}
	// }
};

}  // namespace detail

std::error_category const& channel_category() noexcept {
	static detail::channel_category c;
	return c;
}

inline std::error_code make_error_code(lesomnus::channel::channel_errc e) noexcept {
	return {static_cast<int>(e), lesomnus::channel::channel_category()};
}

}  // namespace channel
}  // namespace lesomnus

namespace std {

template<>
struct is_error_code_enum<lesomnus::channel::channel_errc>: public true_type { };

}  // namespace std

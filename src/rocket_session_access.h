/* RocketRPG session policy. GPL v3 or later, like the modified Player. */
#ifndef EP_ROCKET_SESSION_ACCESS_H
#define EP_ROCKET_SESSION_ACCESS_H

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RocketBridge {
enum class SessionRole { Solo, Host, Guest };

struct SessionAccess {
	SessionRole role = SessionRole::Solo;
	bool can_start = true;
	std::int64_t revision = 0;
};

struct StartRequest {
	std::string id;
	std::int64_t revision;
	std::optional<bool> result;
};

inline bool ResolveStartRequest(StartRequest& request, const SessionAccess& access, std::string_view id,
	std::string_view revision, std::string_view current_revision, std::string_view approved) {
	if (request.result.has_value() || id != request.id || revision != std::to_string(request.revision) ||
		current_revision != std::to_string(access.revision) || access.role != SessionRole::Host ||
		(approved != "0" && approved != "1") || (approved == "1" && !access.can_start)) return false;
	request.result = approved == "1";
	return true;
}

inline std::optional<SessionAccess> ParseSessionAccess(std::string_view role, std::string_view allow, std::string_view revision) {
	SessionAccess value;
	if (role == "solo") value.role = SessionRole::Solo;
	else if (role == "host") value.role = SessionRole::Host;
	else if (role == "guest") value.role = SessionRole::Guest;
	else return std::nullopt;
	if (allow != "0" && allow != "1") return std::nullopt;
	if (revision.empty()) return std::nullopt;
	auto parsed = std::from_chars(revision.data(), revision.data() + revision.size(), value.revision);
	if (parsed.ec != std::errc{} || parsed.ptr != revision.data() + revision.size() || value.revision < 0) return std::nullopt;
	value.can_start = value.role == SessionRole::Solo || (value.role == SessionRole::Host && allow == "1");
	return value;
}

inline bool ApplySessionAccess(SessionAccess& current, const SessionAccess& next) {
	if (next.revision <= current.revision) return false;
	current = next;
	return true;
}
}
#endif

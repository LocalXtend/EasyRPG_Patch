#include "rocket_session_access.h"
#include "doctest.h"

TEST_CASE("RocketRPG session permissions") {
	using namespace RocketBridge;
	auto solo = ParseSessionAccess("solo", "0", "1");
	REQUIRE(solo);
	CHECK(solo->can_start);
	auto guest = ParseSessionAccess("guest", "1", "2");
	REQUIRE(guest);
	CHECK_FALSE(guest->can_start);
	auto host = ParseSessionAccess("host", "0", "3");
	REQUIRE(host);
	CHECK_FALSE(host->can_start);
	CHECK(ApplySessionAccess(*guest, *host));
	CHECK(guest->role == SessionRole::Host);
	CHECK_FALSE(ApplySessionAccess(*guest, *solo));
	CHECK_FALSE(ApplySessionAccess(*guest, *host));
	CHECK(ParseSessionAccess("host", "1", "4")->can_start);
	for (const auto* invalid : {"", "-1", "1.5", "+1", "1junk", "9223372036854775808"}) {
		CHECK_FALSE(ParseSessionAccess("host", "1", invalid));
	}
	CHECK_FALSE(ParseSessionAccess("unknown", "1", "5"));
	CHECK_FALSE(ParseSessionAccess("guest", "true", "5"));
}

TEST_CASE("RocketRPG start decisions bind the exact request and current host permission") {
	using namespace RocketBridge;
	StartRequest request{"0123456789abcdef0123456789abcdef", 3, std::nullopt};
	SessionAccess host{SessionRole::Host, false, 3};
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "3", "3", "1"));
	CHECK_FALSE(request.result.has_value());
	host = {SessionRole::Host, true, 4};
	CHECK_FALSE(ResolveStartRequest(request, host, "old", "3", "4", "1"));
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "2", "4", "1"));
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "3", "3", "1"));
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "3", "4", "true"));
	CHECK_FALSE(ResolveStartRequest(request, {SessionRole::Guest, false, 4}, request.id, "3", "4", "0"));
	CHECK_FALSE(ResolveStartRequest(request, {SessionRole::Solo, true, 4}, request.id, "3", "4", "1"));
	CHECK_FALSE(request.result.has_value());
	CHECK(ResolveStartRequest(request, host, request.id, "3", "4", "1"));
	CHECK(request.result.value_or(false));
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "3", "4", "1"));
	CHECK_FALSE(ResolveStartRequest(request, host, request.id, "3", "4", "0"));
	request.result.reset();
	host.can_start = false;
	CHECK(ResolveStartRequest(request, host, request.id, "3", "4", "0"));
	CHECK(request.result.has_value());
	CHECK_FALSE(*request.result);
}

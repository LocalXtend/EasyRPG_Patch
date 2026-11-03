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

// not upstream: CollisionAPI.h in a process without the addon (Design CA E4 9 P4); the E3-U12 id stays with E3's CollDamageA.Test (E4 7.9.3)
#include "CollisionAPI.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("CollisionAPI.h without the addon (P4)")
{
	CHECK (COLLA_VMSG == 0x4F434F4C);
	CHECK (COLLA_MAGIC == 0x31414C43u);
	CHECK (sizeof (COLLA_EXPORTS) / sizeof (COLLA_EXPORTS[0]) == 5);
	CHECK (collaFind ("collaVersion") == nullptr); // the addon is not loaded in this process
}

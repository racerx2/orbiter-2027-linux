// not upstream: CollisionAPI.h alone in a translation unit, with the layout checks of Design CA E3 6.1 (first part of E3-U12)
#include "CollisionAPI.h"
#include <cstddef>
#include <catch2/catch_test_macros.hpp>

static_assert (sizeof (COLLA_HDR) == 12);
static_assert (offsetof (COLLA_CONTACTINFO, hdr) == 0);
static_assert (offsetof (COLLA_DAMAGEINFO, hdr) == 0);
static_assert (sizeof (void *) != 8 || (sizeof (COLLA_CONTACTINFO) == 128 && sizeof (COLLA_DAMAGEINFO) == 128));
static_assert (sizeof (void *) != 8 || (offsetof (COLLA_CONTACTINFO, hOther) == 16 && offsetof (COLLA_CONTACTINFO, simt) == 40));
static_assert (sizeof (void *) != 8 || (offsetof (COLLA_DAMAGEINFO, hOther) == 16 && offsetof (COLLA_DAMAGEINFO, simt) == 40));

TEST_CASE ("E3-U12 header alone (P4 part)")
{
	CHECK (COLLA_VMSG == 0x4F434F4C);
	CHECK (COLLA_MAGIC == 0x31414C43u);
	CHECK (sizeof (COLLA_EXPORTS) / sizeof (COLLA_EXPORTS[0]) == 5);
	CHECK (collaFind ("collaVersion") == nullptr); // the addon is not loaded in this process
}

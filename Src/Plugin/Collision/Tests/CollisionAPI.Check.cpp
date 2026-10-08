// not upstream: CollisionAPI.h alone in a translation unit with the layout checks of Design CA E3 6.1; no Catch2, so the 2024 header check compiles it too
#include "CollisionAPI.h"
#include <cstddef>

static_assert (sizeof (COLLA_HDR) == 12);
static_assert (offsetof (COLLA_CONTACTINFO, hdr) == 0);
static_assert (offsetof (COLLA_DAMAGEINFO, hdr) == 0);
static_assert (sizeof (void *) != 8 || (sizeof (COLLA_CONTACTINFO) == 128 && sizeof (COLLA_DAMAGEINFO) == 128));
static_assert (sizeof (void *) != 8 || (offsetof (COLLA_CONTACTINFO, hOther) == 16 && offsetof (COLLA_CONTACTINFO, simt) == 40));
static_assert (sizeof (void *) != 8 || (offsetof (COLLA_DAMAGEINFO, hOther) == 16 && offsetof (COLLA_DAMAGEINFO, simt) == 40));

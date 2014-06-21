/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* TRIM/discard interoperation subsystem for reiser4. */

#if !defined(__FS_REISER4_DISCARD_H__)
#define __FS_REISER4_DISCARD_H__

#include "forward.h"
#include "dformat.h"

/**
 * Issue discard requests for all block extents recorded in @atom's delete sets,
 * if discard is enabled. In this case the delete sets are cleared.
 *
 * @atom should be locked on entry and is unlocked on exit.
 */
extern int discard_atom(txn_atom *atom);

/* __FS_REISER4_DISCARD_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/

#ifndef PLAYER_MASK_H_
#define PLAYER_MASK_H_

#include "ctp2_inttypes.h"

// Neutral/unassigned objects have no player bit, including during ownership changes.
inline uint32 player_Mask(sint32 owner)
{
    return owner >= 0 && owner < 32 ? (uint32(1) << owner) : uint32(0);
}

#endif

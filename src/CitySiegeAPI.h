#pragma once
// Stub header — mod-city-siege is not installed.
// All functions return safe defaults so mod-nemesis-system compiles without it.

#include "ObjectGuid.h"

namespace CitySiegeAPI
{
    enum class SiegeParticipantRole
    {
        None = 0
    };

    inline SiegeParticipantRole GetActiveCreatureRole(ObjectGuid /*guid*/)
    {
        return SiegeParticipantRole::None;
    }
}

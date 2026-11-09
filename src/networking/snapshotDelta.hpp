#pragma once

/** @file
 *  Per-entity deltas between sceneReplicator snapshots. Internal to the engine.
 *
 *  The server splits each published snapshot into a snapshotTable and sends a client
 *  only the records that differ from the snapshot that client last acknowledged. The
 *  client applies the delta to its own copy of that snapshot and joins it back into a
 *  full snapshot for sceneReplicator::apply(), so despawn-by-omission still works.
 *
 *  delta     u32 changed, changed x record, u32 removed, removed x NetId
 *
 *  A delta against an empty table is every record, i.e. a full snapshot.
 */

#include <map>

#include "engine/ids.hpp"
#include "engine/networking/bytes.hpp"

namespace engine::networking {

    /** One snapshot split per entity: NetId -> that entity's encoded record. */
    using snapshotTable = std::map<NetId, Bytes>;

    /** Splits a full snapshot. Returns false, leaving table empty, if it is malformed. */
    bool splitSnapshot(ByteView snapshot, snapshotTable& table);

    /** Rebuilds a full snapshot from a table. */
    Bytes joinSnapshot(const snapshotTable& table);

    /** Records in current that are new or differ from base, and NetIds in base that
     *  current no longer has. */
    Bytes encodeDelta(const snapshotTable& base, const snapshotTable& current);

    /** Applies a delta from encodeDelta() to table. Returns false, changing nothing, if
     *  the delta is malformed. */
    bool applyDelta(snapshotTable& table, ByteView delta);

}

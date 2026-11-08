#include "snapshotDelta.hpp"
#include "snapshotFormat.hpp"

#include <utility>
#include <vector>

namespace engine::networking {

    namespace {
        // Reads count records, keeping each one's raw bytes under its NetId.
        bool readRecords(byteReader& in, ByteView data, std::uint32_t count,
                         std::vector<std::pair<NetId, Bytes>>& records)
        {
            for(std::uint32_t i = 0; i < count; ++i)
            {
                const std::size_t start = in.position();
                entityRecord r;
                if(!readRecord(in, r)) {return false;}
                records.emplace_back(r.id, Bytes(data.begin() + start, data.begin() + in.position()));
            }
            return true;
        }
    }

    bool splitSnapshot(ByteView snapshot, snapshotTable& table)
    {
        table.clear();
        byteReader in(snapshot);
        std::uint8_t version = 0;
        std::uint32_t count = 0;
        in.get(version);
        in.get(count);
        if(!in.ok() || version != kFormatVersion) {return false;}

        std::vector<std::pair<NetId, Bytes>> records;
        if(!readRecords(in, snapshot, count, records) || !in.atEnd()) {return false;}
        for(auto& [id, record] : records) {table[id] = std::move(record);}
        return true;
    }

    Bytes joinSnapshot(const snapshotTable& table)
    {
        Bytes out;
        byteWriter w(out);
        w.put(kFormatVersion);
        w.put(static_cast<std::uint32_t>(table.size()));
        for(const auto& [id, record] : table) {out.insert(out.end(), record.begin(), record.end());}
        return out;
    }

    Bytes encodeDelta(const snapshotTable& base, const snapshotTable& current)
    {
        Bytes changed;
        std::uint32_t changedCount = 0;
        for(const auto& [id, record] : current)
        {
            auto old = base.find(id);
            if(old != base.end() && old->second == record) {continue;}
            changed.insert(changed.end(), record.begin(), record.end());
            ++changedCount;
        }

        std::vector<NetId> removed;
        for(const auto& [id, record] : base)
        {
            if(!current.contains(id)) {removed.push_back(id);}
        }

        Bytes out;
        byteWriter w(out);
        w.put(changedCount);
        out.insert(out.end(), changed.begin(), changed.end());
        w.put(static_cast<std::uint32_t>(removed.size()));
        for(NetId id : removed) {w.put(id);}
        return out;
    }

    bool applyDelta(snapshotTable& table, ByteView delta)
    {
        // parse everything first so a malformed delta changes nothing
        byteReader in(delta);
        std::uint32_t changedCount = 0;
        if(!in.get(changedCount)) {return false;}

        std::vector<std::pair<NetId, Bytes>> changed;
        if(!readRecords(in, delta, changedCount, changed)) {return false;}

        std::uint32_t removedCount = 0;
        in.get(removedCount);
        std::vector<NetId> removed;
        for(std::uint32_t i = 0; i < removedCount && in.ok(); ++i)
        {
            NetId id = 0;
            in.get(id);
            removed.push_back(id);
        }
        if(!in.ok() || !in.atEnd()) {return false;}

        for(auto& [id, record] : changed) {table[id] = std::move(record);}
        for(NetId id : removed) {table.erase(id);}
        return true;
    }

}

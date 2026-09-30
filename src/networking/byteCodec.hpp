#pragma once

/** @file
 *  Minimal append/read helpers for building wire payloads. Values are copied in host
 *  byte order; every process in a session must share an architecture.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

#include "engine/networking/bytes.hpp"

namespace engine::networking {

    class byteWriter {
        public:
        explicit byteWriter(Bytes& out): out_(out) {}

        template <typename T>
        void put(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            auto at = out_.size();
            out_.resize(at + sizeof(T));
            std::memcpy(out_.data() + at, &value, sizeof(T));
        }

        /** Length-prefixed (u32) byte run. */
        void putBytes(ByteView data)
        {
            put(static_cast<std::uint32_t>(data.size()));
            out_.insert(out_.end(), data.begin(), data.end());
        }

        void putString(const std::string& s)
        {
            putBytes(ByteView{reinterpret_cast<const std::byte*>(s.data()), s.size()});
        }

        private:
        Bytes& out_;
    };

    /** Reads fail (and stay failed) once the input runs out, so a malformed payload can
     *  be read straight through and checked once with ok(). */
    class byteReader {
        public:
        explicit byteReader(ByteView in): in_(in) {}

        template <typename T>
        bool get(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if(!ok_ || in_.size() - pos_ < sizeof(T)) {ok_ = false; return false;}
            std::memcpy(&value, in_.data() + pos_, sizeof(T));
            pos_ += sizeof(T);
            return true;
        }

        bool getBytes(Bytes& data)
        {
            std::uint32_t size = 0;
            if(!get(size)) {return false;}
            if(in_.size() - pos_ < size) {ok_ = false; return false;}
            data.assign(in_.begin() + pos_, in_.begin() + pos_ + size);
            pos_ += size;
            return true;
        }

        bool getString(std::string& s)
        {
            Bytes data;
            if(!getBytes(data)) {return false;}
            s.assign(reinterpret_cast<const char*>(data.data()), data.size());
            return true;
        }

        bool ok() const {return ok_;}
        bool atEnd() const {return pos_ == in_.size();}

        private:
        ByteView in_;
        std::size_t pos_ = 0;
        bool ok_ = true;
    };

}

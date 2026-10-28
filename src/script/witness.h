// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2026 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVEN_SCRIPT_WITNESS_H
#define RAVEN_SCRIPT_WITNESS_H

#include "serialize.h"

#include <algorithm>
#include <assert.h>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <utility>
#include <vector>

/** A non-owning, immutable view of one witness stack element. */
class CWitnessElementView
{
private:
    const unsigned char* m_data;
    size_t m_size;

    static const unsigned char* EmptyData()
    {
        static const unsigned char empty = 0;
        return &empty;
    }

public:
    typedef const unsigned char* const_iterator;

    CWitnessElementView() : m_data(EmptyData()), m_size(0) {}
    CWitnessElementView(const unsigned char* data, size_t size) :
        m_data(size == 0 ? EmptyData() : data), m_size(size)
    {
        assert(size == 0 || data != nullptr);
    }

    const_iterator begin() const { return m_data; }
    const_iterator end() const { return m_data + m_size; }
    const unsigned char* data() const { return m_data; }
    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    const unsigned char& operator[](size_t index) const
    {
        assert(index < m_size);
        return m_data[index];
    }

    operator std::vector<unsigned char>() const
    {
        return std::vector<unsigned char>(begin(), end());
    }

    friend bool operator==(const CWitnessElementView& a,
                           const CWitnessElementView& b)
    {
        return a.m_size == b.m_size &&
               (a.m_size == 0 || std::memcmp(a.m_data, b.m_data, a.m_size) == 0);
    }

    friend bool operator!=(const CWitnessElementView& a,
                           const CWitnessElementView& b)
    {
        return !(a == b);
    }
};

/**
 * Witness stack with a compact immutable representation for deserialized data.
 *
 * The historical vector<vector<unsigned char>> representation consumes one
 * vector object per wire element. Millions of empty elements can therefore
 * expand a 16-MB message into hundreds of MiB before script validation. This
 * class stores the canonical element encoding contiguously and one 32-bit
 * checkpoint per 256 elements. Read-only access returns lightweight views.
 * Locally constructed or explicitly mutated stacks retain vector semantics.
 */
class CWitnessStack
{
public:
    typedef std::vector<unsigned char> value_type;
    typedef size_t size_type;

private:
    enum {
        CHECKPOINT_INTERVAL = 256,
        READ_CHUNK_SIZE = 64 * 1024,
    };

    bool m_compact;
    size_t m_compactSize;
    std::vector<unsigned char> m_serializedElements;
    std::vector<uint32_t> m_checkpoints;
    std::vector<value_type> m_expanded;

    static void AppendCompactSize(std::vector<unsigned char>& out, uint64_t size)
    {
        if (size < 253) {
            out.push_back(static_cast<unsigned char>(size));
        } else if (size <= std::numeric_limits<uint16_t>::max()) {
            out.push_back(253);
            out.push_back(static_cast<unsigned char>(size));
            out.push_back(static_cast<unsigned char>(size >> 8));
        } else if (size <= std::numeric_limits<uint32_t>::max()) {
            out.push_back(254);
            for (unsigned int shift = 0; shift < 32; shift += 8) {
                out.push_back(static_cast<unsigned char>(size >> shift));
            }
        } else {
            out.push_back(255);
            for (unsigned int shift = 0; shift < 64; shift += 8) {
                out.push_back(static_cast<unsigned char>(size >> shift));
            }
        }
    }

    static bool DecodeCompactSize(const std::vector<unsigned char>& encoded,
                                  size_t offset, uint64_t& size,
                                  size_t& encodedSize)
    {
        if (offset >= encoded.size()) {
            return false;
        }
        const uint8_t marker = encoded[offset];
        if (marker < 253) {
            size = marker;
            encodedSize = 1;
            return true;
        }

        encodedSize = marker == 253 ? 3 : marker == 254 ? 5 : 9;
        if (encodedSize > encoded.size() - offset) {
            return false;
        }
        size = 0;
        for (size_t i = 1; i < encodedSize; ++i) {
            size |= uint64_t(encoded[offset + i]) << (8 * (i - 1));
        }
        return size <= encoded.size() - offset - encodedSize;
    }

    CWitnessElementView ViewAtCompactOffset(size_t offset) const
    {
        uint64_t elementSize = 0;
        size_t prefixSize = 0;
        const bool valid = DecodeCompactSize(m_serializedElements, offset,
                                             elementSize, prefixSize);
        assert(valid);
        if (!valid) {
            throw std::ios_base::failure("Corrupt compact witness stack");
        }
        return CWitnessElementView(m_serializedElements.data() + offset + prefixSize,
                                   static_cast<size_t>(elementSize));
    }

    size_t NextCompactOffset(size_t offset) const
    {
        uint64_t elementSize = 0;
        size_t prefixSize = 0;
        const bool valid = DecodeCompactSize(m_serializedElements, offset,
                                             elementSize, prefixSize);
        assert(valid);
        if (!valid) {
            throw std::ios_base::failure("Corrupt compact witness stack");
        }
        return offset + prefixSize + static_cast<size_t>(elementSize);
    }

    CWitnessElementView GetView(size_t index) const
    {
        assert(index < size());
        if (!m_compact) {
            const value_type& element = m_expanded[index];
            return CWitnessElementView(element.data(), element.size());
        }

        const size_t checkpoint = index / CHECKPOINT_INTERVAL;
        assert(checkpoint < m_checkpoints.size());
        size_t offset = m_checkpoints[checkpoint];
        size_t current = checkpoint * CHECKPOINT_INTERVAL;
        while (current < index) {
            offset = NextCompactOffset(offset);
            ++current;
        }
        return ViewAtCompactOffset(offset);
    }

    void EnsureExpanded()
    {
        if (!m_compact) {
            return;
        }

        std::vector<value_type> expanded;
        expanded.reserve(m_compactSize);
        size_t offset = 0;
        for (size_t i = 0; i < m_compactSize; ++i) {
            const CWitnessElementView element = ViewAtCompactOffset(offset);
            expanded.emplace_back(element.begin(), element.end());
            offset = NextCompactOffset(offset);
        }
        assert(offset == m_serializedElements.size());

        m_expanded.swap(expanded);
        std::vector<unsigned char>().swap(m_serializedElements);
        std::vector<uint32_t>().swap(m_checkpoints);
        m_compactSize = 0;
        m_compact = false;
    }

public:
    class const_iterator
    {
    private:
        const CWitnessStack* m_stack;
        size_t m_index;
        size_t m_offset;

    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef CWitnessElementView value_type;
        typedef ptrdiff_t difference_type;
        typedef void pointer;
        typedef CWitnessElementView reference;

        const_iterator() : m_stack(nullptr), m_index(0), m_offset(0) {}
        const_iterator(const CWitnessStack* stack, size_t index, size_t offset) :
            m_stack(stack), m_index(index), m_offset(offset) {}

        CWitnessElementView operator*() const
        {
            assert(m_stack != nullptr && m_index < m_stack->size());
            return m_stack->m_compact
                       ? m_stack->ViewAtCompactOffset(m_offset)
                       : m_stack->GetView(m_index);
        }

        const_iterator& operator++()
        {
            assert(m_stack != nullptr && m_index < m_stack->size());
            if (m_stack->m_compact) {
                m_offset = m_stack->NextCompactOffset(m_offset);
            }
            ++m_index;
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator copy(*this);
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b)
        {
            return a.m_stack == b.m_stack && a.m_index == b.m_index;
        }

        friend bool operator!=(const const_iterator& a, const const_iterator& b)
        {
            return !(a == b);
        }
    };

    CWitnessStack() : m_compact(false), m_compactSize(0) {}
    CWitnessStack(const CWitnessStack&) = default;
    CWitnessStack(CWitnessStack&& other) noexcept :
        m_compact(false), m_compactSize(0)
    {
        swap(other);
    }
    CWitnessStack& operator=(const CWitnessStack&) = default;
    CWitnessStack& operator=(CWitnessStack&& other) noexcept
    {
        if (this != &other) {
            clear();
            swap(other);
        }
        return *this;
    }

    CWitnessStack& operator=(const std::vector<value_type>& elements)
    {
        m_expanded = elements;
        std::vector<unsigned char>().swap(m_serializedElements);
        std::vector<uint32_t>().swap(m_checkpoints);
        m_compactSize = 0;
        m_compact = false;
        return *this;
    }

    CWitnessStack& operator=(std::vector<value_type>&& elements)
    {
        m_expanded = std::move(elements);
        std::vector<unsigned char>().swap(m_serializedElements);
        std::vector<uint32_t>().swap(m_checkpoints);
        m_compactSize = 0;
        m_compact = false;
        return *this;
    }

    size_t size() const { return m_compact ? m_compactSize : m_expanded.size(); }
    bool empty() const { return size() == 0; }

    CWitnessElementView operator[](size_t index) const { return GetView(index); }
    value_type& operator[](size_t index)
    {
        EnsureExpanded();
        return m_expanded[index];
    }

    CWitnessElementView front() const { return GetView(0); }
    CWitnessElementView back() const { return GetView(size() - 1); }
    value_type& front()
    {
        EnsureExpanded();
        return m_expanded.front();
    }
    value_type& back()
    {
        EnsureExpanded();
        return m_expanded.back();
    }

    const_iterator begin() const { return const_iterator(this, 0, 0); }
    const_iterator end() const
    {
        return const_iterator(this, size(),
                              m_compact ? m_serializedElements.size() : 0);
    }
    const_iterator begin() { return const_iterator(this, 0, 0); }
    const_iterator end()
    {
        return const_iterator(this, size(),
                              m_compact ? m_serializedElements.size() : 0);
    }

    void clear()
    {
        m_expanded.clear();
        std::vector<unsigned char>().swap(m_serializedElements);
        std::vector<uint32_t>().swap(m_checkpoints);
        m_compactSize = 0;
        m_compact = false;
    }

    void shrink_to_fit()
    {
        m_expanded.shrink_to_fit();
        m_serializedElements.shrink_to_fit();
        m_checkpoints.shrink_to_fit();
    }

    void resize(size_t count)
    {
        EnsureExpanded();
        m_expanded.resize(count);
    }

    void push_back(const value_type& value)
    {
        EnsureExpanded();
        m_expanded.push_back(value);
    }

    void push_back(value_type&& value)
    {
        EnsureExpanded();
        m_expanded.push_back(std::move(value));
    }

    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        EnsureExpanded();
        m_expanded.emplace_back(std::forward<Args>(args)...);
    }

    std::vector<value_type> ToVector() const
    {
        if (!m_compact) {
            return m_expanded;
        }
        std::vector<value_type> result;
        result.reserve(m_compactSize);
        for (const CWitnessElementView element : *this) {
            result.emplace_back(element.begin(), element.end());
        }
        return result;
    }

    operator std::vector<value_type>() const { return ToVector(); }

    /** Dynamic bytes owned by the representation, excluding allocator metadata. */
    size_t DynamicMemoryUsage() const
    {
        size_t usage = m_serializedElements.capacity() * sizeof(unsigned char) +
                       m_checkpoints.capacity() * sizeof(uint32_t) +
                       m_expanded.capacity() * sizeof(value_type);
        for (const value_type& element : m_expanded) {
            usage += element.capacity() * sizeof(unsigned char);
        }
        return usage;
    }

    bool IsCompact() const { return m_compact; }

    void swap(CWitnessStack& other)
    {
        std::swap(m_compact, other.m_compact);
        std::swap(m_compactSize, other.m_compactSize);
        m_serializedElements.swap(other.m_serializedElements);
        m_checkpoints.swap(other.m_checkpoints);
        m_expanded.swap(other.m_expanded);
    }

    template <typename Stream>
    void Serialize(Stream& stream) const
    {
        WriteCompactSize(stream, size());
        if (m_compact) {
            if (!m_serializedElements.empty()) {
                stream.write(reinterpret_cast<const char*>(m_serializedElements.data()),
                             m_serializedElements.size());
            }
            return;
        }
        for (const value_type& element : m_expanded) {
            ::Serialize(stream, element);
        }
    }

    template <typename Stream>
    void Unserialize(Stream& stream)
    {
        CWitnessStack parsed;
        parsed.m_compact = true;
        parsed.m_compactSize = ReadCompactSize(stream);

        for (size_t i = 0; i < parsed.m_compactSize; ++i) {
            if (i % CHECKPOINT_INTERVAL == 0) {
                if (parsed.m_serializedElements.size() >
                    std::numeric_limits<uint32_t>::max()) {
                    throw std::ios_base::failure("Witness checkpoint offset overflow");
                }
                parsed.m_checkpoints.push_back(
                    static_cast<uint32_t>(parsed.m_serializedElements.size()));
            }

            const uint64_t elementSize = ReadCompactSize(stream);
            const size_t prefixSize = GetSizeOfCompactSize(elementSize);
            if (prefixSize > MAX_SIZE - parsed.m_serializedElements.size() ||
                elementSize > MAX_SIZE - parsed.m_serializedElements.size() - prefixSize) {
                throw std::ios_base::failure("Witness stack encoding exceeds size limit");
            }
            AppendCompactSize(parsed.m_serializedElements, elementSize);

            uint64_t remaining = elementSize;
            while (remaining != 0) {
                const size_t chunk = static_cast<size_t>(
                    std::min<uint64_t>(remaining, READ_CHUNK_SIZE));
                const size_t oldSize = parsed.m_serializedElements.size();
                parsed.m_serializedElements.resize(oldSize + chunk);
                stream.read(
                    reinterpret_cast<char*>(parsed.m_serializedElements.data() + oldSize),
                    chunk);
                remaining -= chunk;
            }
        }

        swap(parsed);
    }

    friend bool operator==(const CWitnessStack& a, const CWitnessStack& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        const_iterator ai = a.begin();
        const_iterator bi = b.begin();
        for (; ai != a.end(); ++ai, ++bi) {
            if (*ai != *bi) {
                return false;
            }
        }
        return true;
    }

    friend bool operator!=(const CWitnessStack& a, const CWitnessStack& b)
    {
        return !(a == b);
    }
};

#endif // RAVEN_SCRIPT_WITNESS_H

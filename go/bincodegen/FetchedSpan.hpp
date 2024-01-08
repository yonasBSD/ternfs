
struct FetchedSpan {
public:
    FetchedSpanHeader header;
private:
    std::variant<FetchedInlineSpan, FetchedBlocksSpan> body;

public:
    static constexpr uint16_t STATIC_SIZE = FetchedSpanHeader::STATIC_SIZE;

    FetchedSpan() { clear(); }

    const FetchedInlineSpan& getInlineSpan() const {
        ALWAYS_ASSERT(header.storageClass == INLINE_STORAGE);
        return std::get<0>(body);
    }
    FetchedInlineSpan& setInlineSpan() {
        header.storageClass = INLINE_STORAGE;
        return body.emplace<0>();
    }

    const FetchedBlocksSpan& getBlocksSpan() const {
        ALWAYS_ASSERT(header.storageClass > INLINE_STORAGE);
        return std::get<1>(body);
    }
    FetchedBlocksSpan& setBlocksSpan(uint8_t s) {
        ALWAYS_ASSERT(s > INLINE_STORAGE);
        header.storageClass = s;
        return body.emplace<1>();
    }

    void clear() {
        header.clear();
    }

    size_t packedSize() const {
        ALWAYS_ASSERT(header.storageClass != 0);
        size_t size = STATIC_SIZE;
        if (header.storageClass == INLINE_STORAGE) {
            size += getInlineSpan().packedSize();
        } else if (header.storageClass > INLINE_STORAGE) {
            size += getBlocksSpan().packedSize();
        }
        return size;
    }

    void pack(BincodeBuf& buf) const {
        ALWAYS_ASSERT(header.storageClass != 0);
        header.pack(buf);
        if (header.storageClass == INLINE_STORAGE) {
            getInlineSpan().pack(buf);
        } else if (header.storageClass > INLINE_STORAGE) {
            getBlocksSpan().pack(buf);
        }
    }

    void unpack(BincodeBuf& buf) {
        header.unpack(buf);
        if (header.storageClass == 0) {
            throw BINCODE_EXCEPTION("Unexpected EMPTY storage class");
        }
        if (header.storageClass == INLINE_STORAGE) {
            setInlineSpan().unpack(buf);
        } else if (header.storageClass > INLINE_STORAGE) {
            setBlocksSpan(header.storageClass).unpack(buf);
        }
    }

    bool operator==(const FetchedSpan& other) const {
        if (header != other.header) {
            return false;
        }
        ALWAYS_ASSERT(header.storageClass != 0);
        if (header.storageClass != other.header.storageClass) {
            return false;
        }
        if (header.storageClass == INLINE_STORAGE) {
            return getInlineSpan() == other.getInlineSpan();
        } else if (header.storageClass > INLINE_STORAGE) {
            return getBlocksSpan() == other.getBlocksSpan();
        }
        return true;
    }
};

UNUSED
static std::ostream& operator<<(std::ostream& out, const FetchedSpan& span) {
    ALWAYS_ASSERT(span.header.storageClass != 0);
    out << "FetchedSpan(" << "Header=" << span.header;
    if (span.header.storageClass == INLINE_STORAGE) {
        out << ", Body=" << span.getInlineSpan();
    } else if (span.header.storageClass > INLINE_STORAGE) {
        out << ", Body=" << span.getBlocksSpan();
    }
    out << ")";
    return out;
}

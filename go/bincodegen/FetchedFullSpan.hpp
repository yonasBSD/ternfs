
struct FetchedFullSpan {
public:
    FetchedSpanHeaderFull header;
private:
    std::variant<FetchedInlineSpan, FetchedLocations> body;

public:
    static constexpr uint16_t STATIC_SIZE = FetchedSpanHeaderFull::STATIC_SIZE;

    FetchedFullSpan() { clear(); }

    const FetchedInlineSpan& getInlineSpan() const {
        ALWAYS_ASSERT(header.isInline);
        return std::get<0>(body);
    }
    FetchedInlineSpan& setInlineSpan() {
        header.isInline = true;
        return body.emplace<0>();
    }

    const FetchedLocations& getLocations() const {
        ALWAYS_ASSERT(!header.isInline);
        return std::get<1>(body);
    }
    FetchedLocations& setLocations() {
        header.isInline = false;
        return body.emplace<1>();
    }

    void clear() {
        header.clear();
    }

    size_t packedSize() const {
        size_t size = STATIC_SIZE;
        if (header.isInline) {
            size += getInlineSpan().packedSize();
        } else {
            size += getLocations().packedSize();
        }
        return size;
    }

    void pack(BincodeBuf& buf) const {
        header.pack(buf);
        if (header.isInline) {
            getInlineSpan().pack(buf);
        } else {
            getLocations().pack(buf);
        }
    }

    void unpack(BincodeBuf& buf) {
        header.unpack(buf);
        if (header.isInline) {
            setInlineSpan().unpack(buf);
        } else {
            setLocations().unpack(buf);
        }
    }

    bool operator==(const FetchedFullSpan& other) const {
        if (header != other.header) {
            return false;
        }

        if (header.isInline) {
            return getInlineSpan() == other.getInlineSpan();
        } else {
            return getLocations() == other.getLocations();
        }
        return true;
    }
};

UNUSED
static std::ostream& operator<<(std::ostream& out, const FetchedFullSpan& span) {
    out << "FetchedFullSpan(" << "Header=" << span.header;
    if (span.header.isInline) {
        out << ", Body=" << span.getInlineSpan();
    } else {
        out << ", Body=" << span.getLocations();
    }
    out << ")";
    return out;
}

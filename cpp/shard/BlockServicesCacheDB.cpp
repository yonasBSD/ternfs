#include <rocksdb/db.h>

#include "BlockServicesCacheDB.hpp"
#include "RocksDBUtils.hpp"

enum class BlockServicesCacheKey : uint8_t {
    CURRENT_BLOCK_SERVICES = 0,
    BLOCK_SERVICE = 1, // postfixed with the block service id
};

constexpr BlockServicesCacheKey CURRENT_BLOCK_SERVICES_KEY = BlockServicesCacheKey::CURRENT_BLOCK_SERVICES;
constexpr BlockServicesCacheKey BLOCK_SERVICE_KEY = BlockServicesCacheKey::BLOCK_SERVICE;

inline rocksdb::Slice blockServicesCacheKey(const BlockServicesCacheKey* k) {
    ALWAYS_ASSERT(*k != BLOCK_SERVICE_KEY);
    return rocksdb::Slice((const char*)k, sizeof(*k));
}

struct CurrentBlockServicesBody {
    FIELDS(
        LE, uint8_t, length, setLength,
        EMIT_OFFSET, MIN_SIZE,
        END
    )

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE, "sz < MIN_SIZE (%s < %s)", sz, MIN_SIZE);
        ALWAYS_ASSERT(sz == size(), "sz != size() (%s, %s)", sz, size());
    }

    static size_t calcSize(uint64_t numBlockServices) {
        ALWAYS_ASSERT(numBlockServices < 256);
        return MIN_SIZE + numBlockServices*sizeof(uint64_t);
    }

    void afterAlloc(uint64_t numBlockServices) {
        setLength(numBlockServices);
    }

    size_t size() const {
        return MIN_SIZE + length()*sizeof(uint64_t);
    }

    uint64_t at(uint64_t ix) const {
        ALWAYS_ASSERT(ix < length());
        uint64_t v;
        memcpy(&v, _data + MIN_SIZE + (ix*sizeof(uint64_t)), sizeof(uint64_t));
        return v;
    }

    void set(uint64_t ix, uint64_t v) {
        ALWAYS_ASSERT(ix < length());
        memcpy(_data + MIN_SIZE + (ix*sizeof(uint64_t)), &v, sizeof(uint64_t));
    }
};

struct BlockServiceKey {
    FIELDS(
        BE, BlockServicesCacheKey, key, setKey, // always BLOCK_SERVICE_KEY
        BE, uint64_t,              blockServiceId, setBlockServiceId,
        END_STATIC
    )
};

struct BlockServiceBody {
    FIELDS(
        LE, uint8_t,  version,          setVersion,
        LE, uint64_t, id,               setId,
        FBYTES, 4,    ip1,              setIp1,
        LE, uint16_t, port1,            setPort1,
        FBYTES, 4,    ip2,              setIp2,
        LE, uint16_t, port2,            setPort2,
        LE, uint8_t,  storageClass,     setStorageClass,
        FBYTES, 16,   failureDomain,    setFailureDomain,
        FBYTES, 16,   secretKey,        setSecretKey,
        EMIT_OFFSET, V0_OFFSET,
        LE, uint8_t, flagsV1, setFlagsV1,
        EMIT_OFFSET, V1_OFFSET,
        END
    )

    static constexpr size_t MAX_SIZE = V1_OFFSET;

    size_t size() const {
        switch (version()) {
        case 0: return V0_OFFSET;
        case 1: return V1_OFFSET;
        default: throw EGGS_EXCEPTION("bad version %s", version());
        }
    }

    void checkSize(size_t s) { ALWAYS_ASSERT(s == size()); }

    uint8_t flags() const {
        if (unlikely(version() == 0)) { return 0; }
        return flagsV1();
    }

    void setFlags(uint8_t f) {
        ALWAYS_ASSERT(version() > 0);
        setFlagsV1(f);
    }
};

std::vector<rocksdb::ColumnFamilyDescriptor> BlockServicesCacheDB::getColumnFamilyDescriptors() {
    return std::vector<rocksdb::ColumnFamilyDescriptor> {{"blockServicesCache", {}}};
}

BlockServicesCacheDB::BlockServicesCacheDB(Logger& logger, std::shared_ptr<XmonAgent>& xmon, const SharedRocksDB& sharedDB) :
    _env(logger, xmon, "bs_cache_db"),
    _db(sharedDB.db()),
    _blockServicesCF(sharedDB.getCF("blockServicesCache"))
{
    LOG_INFO(_env, "Initializing block services cache DB");

    std::unique_lock _(_mutex);

    const auto keyExists = [this](rocksdb::ColumnFamilyHandle* cf, const rocksdb::Slice& key) -> bool {
        std::string value;
        auto status = _db->Get({}, cf, key, &value);
        if (status.IsNotFound()) {
            return false;
        } else {
            ROCKS_DB_CHECKED(status);
            return true;
        }
    };

    if (!keyExists(_blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY))) {
        LOG_INFO(_env, "initializing current block services (as empty)");
        OwnedValue<CurrentBlockServicesBody> v(0);
        v().setLength(0);
        ROCKS_DB_CHECKED(_db->Put({}, _blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY), v.toSlice()));
    } else {
        LOG_INFO(_env, "initializing block services cache (from db)");
        std::string buf;
        ROCKS_DB_CHECKED(_db->Get({}, _blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY), &buf));
        ExternalValue<CurrentBlockServicesBody> v(buf);
        _currentBlockServices.resize(v().length());
        for (int i = 0; i < v().length(); i++) {
            _currentBlockServices[i] = v().at(i);
        }
        {
            rocksdb::ReadOptions options;
            static_assert(sizeof(BlockServicesCacheKey) == sizeof(uint8_t));
            auto upperBound = (BlockServicesCacheKey)((uint8_t)BLOCK_SERVICE_KEY + 1);
            auto upperBoundSlice = blockServicesCacheKey(&upperBound);
            options.iterate_upper_bound = &upperBoundSlice;
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, _blockServicesCF));
            StaticValue<BlockServiceKey> beginKey;
            beginKey().setKey(BLOCK_SERVICE_KEY);
            beginKey().setBlockServiceId(0);
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto k = ExternalValue<BlockServiceKey>::FromSlice(it->key());
                ALWAYS_ASSERT(k().key() == BLOCK_SERVICE_KEY);
                auto v = ExternalValue<BlockServiceBody>::FromSlice(it->value());
                auto& cache = _blockServices[k().blockServiceId()];
                cache.addrs[0].ip = v().ip1();
                cache.addrs[0].port = v().port1();
                cache.addrs[1].ip = v().ip2();
                cache.addrs[1].port = v().port2();
                expandKey(v().secretKey(), cache.secretKey);
                cache.storageClass = v().storageClass();
                cache.flags = v().flags();
            }
            ROCKS_DB_CHECKED(it->status());
        }
        _haveBlockServices = true;
    }
}

BlockServicesCache BlockServicesCacheDB::getCache() const {
    return BlockServicesCache(_mutex, _blockServices, _currentBlockServices);
}

void BlockServicesCacheDB::updateCache(const std::vector<BlockServiceInfo>& blockServices, const std::vector<BlockServiceId>& currentBlockServices) {
    LOG_INFO(_env, "Updating block service cache");

    std::unique_lock _(_mutex);

    rocksdb::WriteBatch batch;
    // fill in main cache first
    StaticValue<BlockServiceKey> blockKey;
    blockKey().setKey(BLOCK_SERVICE_KEY);
    StaticValue<BlockServiceBody> blockBody;
    for (int i = 0; i < blockServices.size(); i++) {
        const auto& entryBlock = blockServices[i];
        blockKey().setBlockServiceId(entryBlock.id.u64);
        blockBody().setVersion(1);
        blockBody().setId(entryBlock.id.u64);
        blockBody().setIp1(entryBlock.addrs[0].ip.data);
        blockBody().setPort1(entryBlock.addrs[0].port);
        blockBody().setIp2(entryBlock.addrs[1].ip.data);
        blockBody().setPort2(entryBlock.addrs[1].port);
        blockBody().setStorageClass(entryBlock.storageClass);
        blockBody().setFailureDomain(entryBlock.failureDomain.name.data);
        blockBody().setSecretKey(entryBlock.secretKey.data);
        blockBody().setFlags(entryBlock.flags);
        ROCKS_DB_CHECKED(batch.Put(_blockServicesCF, blockKey.toSlice(), blockBody.toSlice()));
        auto& cache = _blockServices[entryBlock.id.u64];
        expandKey(entryBlock.secretKey.data, cache.secretKey);
        cache.addrs = entryBlock.addrs;
        cache.storageClass = entryBlock.storageClass;
        cache.failureDomain = entryBlock.failureDomain.name.data;
        cache.flags = entryBlock.flags;
    }
    // then the current block services
    ALWAYS_ASSERT(currentBlockServices.size() < 256); // TODO handle this properly
    _currentBlockServices.clear();
    for (BlockServiceId id: currentBlockServices) {
        _currentBlockServices.emplace_back(id.u64);
    }
    OwnedValue<CurrentBlockServicesBody> currentBody(_currentBlockServices.size());
    for (int i = 0; i < _currentBlockServices.size(); i++) {
        currentBody().set(i, _currentBlockServices[i]);
    }
    ROCKS_DB_CHECKED(batch.Put(_blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY), currentBody.toSlice()));

    // We intentionally do not flush here, it's not critical
    // and shard writes will flush anwyay.
    ROCKS_DB_CHECKED(_db->Write({}, &batch));

    _haveBlockServices = true;
}

bool BlockServicesCacheDB::haveBlockServices() const {
    return _haveBlockServices;
}

#include <rocksdb/db.h>

#include "BlockServicesCacheDB.hpp"
#include "RocksDBUtils.hpp"
#include "ShardDBData.hpp"

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

    // Before we stored this info in the ShardDB, we look at both here.
    rocksdb::ColumnFamilyHandle* defaultCF = sharedDB.getCF(rocksdb::kDefaultColumnFamilyName);
    bool inShardDB = keyExists(defaultCF, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY_DONT_USE));
    if (inShardDB) {
        LOG_INFO(_env, "ShardDB data exists, will read from it");
    }
    bool inBlockServicesCacheDB = keyExists(_blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY));
    if (!inShardDB && !inBlockServicesCacheDB) {
        LOG_INFO(_env, "initializing current block services (as empty)");
        OwnedValue<CurrentBlockServicesBody> v(0);
        v().setLength(0);
        ROCKS_DB_CHECKED(_db->Put({}, _blockServicesCF, blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY), v.toSlice()));
    } else {
        // Here we'll read from the ShardDB or the BlockServicesCacheDB as appropriate.
        rocksdb::ColumnFamilyHandle* cf = inShardDB ? defaultCF : _blockServicesCF;
        rocksdb::Slice currentBlockServicesKey = inShardDB ? shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY_DONT_USE) : blockServicesCacheKey(&CURRENT_BLOCK_SERVICES_KEY);
        BlockServicesCacheKey blockServiceKey = inShardDB ? (BlockServicesCacheKey)BLOCK_SERVICE_KEY_DONT_USE : BLOCK_SERVICE_KEY;
        LOG_INFO(_env, "initializing block services cache (from db)");
        std::string buf;
        ROCKS_DB_CHECKED(_db->Get({}, defaultCF, currentBlockServicesKey, &buf));
        ExternalValue<CurrentBlockServicesBody> v(buf);
        _currentBlockServices.resize(v().length());
        for (int i = 0; i < v().length(); i++) {
            _currentBlockServices[i] = v().at(i);
        }
        {
            rocksdb::ReadOptions options;
            static_assert(sizeof(BlockServicesCacheKey) == sizeof(uint8_t));
            auto upperBound = (BlockServicesCacheKey)((uint8_t)blockServiceKey + 1);
            auto upperBoundSlice = blockServicesCacheKey(&upperBound);
            options.iterate_upper_bound = &upperBoundSlice;
            std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, cf));
            StaticValue<BlockServiceKey> beginKey;
            beginKey().setKey(blockServiceKey);
            beginKey().setBlockServiceId(0);
            for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
                auto k = ExternalValue<BlockServiceKey>::FromSlice(it->key());
                ALWAYS_ASSERT(k().key() == blockServiceKey);
                auto v = ExternalValue<BlockServiceBody>::FromSlice(it->value());
                auto& cache = _blockServices[k().blockServiceId()];
                cache.ip1 = v().ip1();
                cache.port1 = v().port1();
                cache.ip2 = v().ip2();
                cache.port2 = v().port2();
                expandKey(v().secretKey(), cache.secretKey);
                cache.storageClass = v().storageClass();
                cache.flags = v().flags();
            }
            ROCKS_DB_CHECKED(it->status());
        }
        _haveBlockServices = true;
    }

    // Delete the stuff in ShardDB, if it was still there anyway.
    ROCKS_DB_CHECKED(_db->Delete({}, defaultCF, shardMetadataKey(&CURRENT_BLOCK_SERVICES_KEY_DONT_USE)));
    {
        rocksdb::ReadOptions options;
        static_assert(sizeof(ShardMetadataKey) == sizeof(uint8_t));
        auto upperBound = (ShardMetadataKey)((uint8_t)BLOCK_SERVICE_KEY_DONT_USE + 1);
        auto upperBoundSlice = shardMetadataKey(&upperBound);
        options.iterate_upper_bound = &upperBoundSlice;
        std::unique_ptr<rocksdb::Iterator> it(_db->NewIterator(options, defaultCF));
        StaticValue<BlockServiceKey> beginKey;
        beginKey().setKey((BlockServicesCacheKey)BLOCK_SERVICE_KEY_DONT_USE);
        beginKey().setBlockServiceId(0);
        for (it->Seek(beginKey.toSlice()); it->Valid(); it->Next()) {
            auto k = ExternalValue<BlockServiceKey>::FromSlice(it->key());
            ALWAYS_ASSERT(k().key() == (BlockServicesCacheKey)BLOCK_SERVICE_KEY_DONT_USE);
            ROCKS_DB_CHECKED(_db->Delete({}, defaultCF, it->key()));
        }
        ROCKS_DB_CHECKED(it->status());
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
        blockKey().setBlockServiceId(entryBlock.info.id.u64);
        blockBody().setVersion(1);
        blockBody().setId(entryBlock.info.id.u64);
        blockBody().setIp1(entryBlock.info.ip1.data);
        blockBody().setPort1(entryBlock.info.port1);
        blockBody().setIp2(entryBlock.info.ip2.data);
        blockBody().setPort2(entryBlock.info.port2);
        blockBody().setStorageClass(entryBlock.info.storageClass);
        blockBody().setFailureDomain(entryBlock.info.failureDomain.name.data);
        blockBody().setSecretKey(entryBlock.info.secretKey.data);
        blockBody().setFlags(entryBlock.info.flags);
        ROCKS_DB_CHECKED(batch.Put(_blockServicesCF, blockKey.toSlice(), blockBody.toSlice()));
        auto& cache = _blockServices[entryBlock.info.id.u64];
        expandKey(entryBlock.info.secretKey.data, cache.secretKey);
        cache.ip1 = entryBlock.info.ip1.data;
        cache.port1 = entryBlock.info.port1;
        cache.ip2 = entryBlock.info.ip2.data;
        cache.port2 = entryBlock.info.port2;
        cache.storageClass = entryBlock.info.storageClass;
        cache.failureDomain = entryBlock.info.failureDomain.name.data;
        cache.flags = entryBlock.info.flags;
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
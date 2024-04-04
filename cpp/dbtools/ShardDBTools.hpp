#pragma once

#include <string>

class ShardDBTools {
public:
    static void verifyEqual(const std::string& db1Path, const std::string& db2Path);
    static void outputUnreleasedState(const std::string& dbPath);
    static void fsck(const std::string& dbPath);
    static void fixupBadInodes(const std::string& dbPath);
};

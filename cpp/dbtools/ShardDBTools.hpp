#pragma once

#include <string>

class ShardDBTools {
public:
    static void verifyEqual(const std::string& db1Path, const std::string& db2Path);
};

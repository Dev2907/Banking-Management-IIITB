#pragma once

#include <sqlite3.h>

namespace databaseOperations {

inline bool prepareStatement(
    sqlite3* db,
    const char* sql,
    sqlite3_stmt*& stmt
) {
    stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        return true;
    }

    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
    return false;
}

} // namespace databaseOperations
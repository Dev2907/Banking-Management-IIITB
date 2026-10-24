#pragma once

#include <sqlite3.h>
#include <string>

struct administratorRes {
    bool status = false;
    std::string msg;
};

class administratorPortal {
private:
    sqlite3* db_;
    int administratorID_;

    bool prepareStatement(const char* sql, sqlite3_stmt*& stmt) const;
    void rollback() const;

public:
    explicit administratorPortal(sqlite3* db, int administratorID);

    administratorRes createUser(
        const std::string& username,
        const std::string& password,
        int role
    );
    administratorRes modifyUser(
        const std::string& currentUsername,
        const std::string& newUsername,
        int newRole,
        bool isActive
    );
    administratorRes changePassword(
        const std::string& currentPassword,
        const std::string& newPassword
    );
};
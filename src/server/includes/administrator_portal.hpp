#pragma once

#include <sqlite3.h>
#include <string>

using namespace std;
struct administratorRes {
    bool status = false;
    string msg;
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
        const string& username,
        const string& password,
        int role
    );
    administratorRes modifyUser(
        const string& currentUsername,
        const string& newUsername,
        int newRole,
        bool isActive
    );
    administratorRes changePassword(
        const string& currentPassword,
        const string& newPassword
    );
};
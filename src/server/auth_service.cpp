#include "includes/auth_service.hpp"
#include "includes/password_utils.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

static bool isValidRole(int role) {
    return role == static_cast<int>(userRoles::Customer) ||
           role == static_cast<int>(userRoles::Employee) ||
           role == static_cast<int>(userRoles::Manager) ||
           role == static_cast<int>(userRoles::Administrator);
}


authService::authService(sqlite3* db)
    : db_(db) {

    if (db_ == nullptr) {
        throw invalid_argument(
            "Database connection cannot be null"
        );
    }
}


authRes authService::authenticate(
    const string& username,
    const string& password,
    int role
) {
    authRes res{};

    //Validate the input.

    if (username.empty() || username.size() > 64) {
        res.message = "Invalid username";
        return res;
    }

    if (password.empty() || password.size() > 1024) {
        res.message = "Invalid password";
        return res;
    }

    if (!isValidRole(role)) {
        res.message = "Invalid role";
        return res;
    }


    //Prepare the SQL query.

    const char* sql =
        "SELECT user_id, password_hash, role, is_active "
        "FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(
        db_,
        sql,
        -1,
        &stmt,
        nullptr
    );

    if (rc != SQLITE_OK) {
        cerr << "SQLite prepare error: "
             << sqlite3_errmsg(db_) << '\n';

        sqlite3_finalize(stmt);

        res.message = "Authentication service error";
        return res;
    }


    //Bind the username to the query.

    rc = sqlite3_bind_text(
        stmt,
        1,
        username.c_str(),
        static_cast<int>(username.size()),
        SQLITE_TRANSIENT
    );

    if (rc != SQLITE_OK) {
        cerr << "SQLite bind error: "
             << sqlite3_errmsg(db_) << '\n';

        sqlite3_finalize(stmt);

        res.message = "Authentication service error";
        return res;
    }


    //Execute the query.

    rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE) {
        // Username does not exist.
        sqlite3_finalize(stmt);

        res.message = "Invalid credentials";
        return res;
    }

    if (rc != SQLITE_ROW) {
        cerr << "SQLite query error: "
             << sqlite3_errmsg(db_) << '\n';

        sqlite3_finalize(stmt);

        res.message = "Authentication service error";
        return res;
    }


    //Retrieve the user information.

    int userID = sqlite3_column_int(stmt, 0);

    const unsigned char* hashText =
        sqlite3_column_text(stmt, 1);

    int hashLength = sqlite3_column_bytes(stmt, 1);

    string storedHash;

    if (hashText != nullptr && hashLength > 0) {
        storedHash.assign(
            reinterpret_cast<const char*>(hashText),
            static_cast<size_t>(hashLength)
        );
    }

    int storedRole = sqlite3_column_int(stmt, 2);

    int isActive = sqlite3_column_int(stmt, 3);

    sqlite3_finalize(stmt);


    //Validate the stored role.

    if (!isValidRole(storedRole)) {
        cerr << "Invalid role stored for user ID: "
             << userID << '\n';

        res.message = "Authentication service error";
        return res;
    }


    //Verify the password hash.

    bool passwordMatch = verifyPassword(
        storedHash,
        password
    );

    if (!passwordMatch || isActive != 1 ||
        role != storedRole) {

        res.message = "Invalid credentials";
        return res;
    }


    res.success = true;
    res.userID = userID;
    res.role = static_cast<userRoles>(storedRole);
    res.message = "Authenticated successfully \n";

    return res;
}

authRes authService::registerCustomer(
    const string& username,
    const string& password
) {
    authRes res{};
    if (username.empty() || username.size() > 64) {
        res.message = "Invalid username";
        return res;
    }
    if (password.empty() || password.size() > 1024) {
        res.message = "Invalid password";
        return res;
    }

    string passwordHash;
    try {
        passwordHash = hashPassword(password);
    } catch (const exception& error) {
        res.message = error.what();
        return res;
    }

    int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        res.message = sqlite3_errmsg(db_);
        return res;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* userSQL =
        "INSERT INTO users (username, password_hash, role, is_active) "
        "VALUES (?, ?, 1, 1);";
    rc = sqlite3_prepare_v2(db_, userSQL, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.message = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return res;
    }

    int userID = static_cast<int>(sqlite3_last_insert_rowid(db_));
    const char* accountSQL =
        "INSERT INTO accounts (customer_id, balance_cents) VALUES (?, 0);";
    rc = sqlite3_prepare_v2(db_, accountSQL, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 1, userID);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.message = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return res;
    }

    rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        res.message = sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return res;
    }

    res.success = true;
    res.userID = userID;
    res.role = userRoles::Customer;
    res.message = "Customer account created successfully\n";
    return res;
}
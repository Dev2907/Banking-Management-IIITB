#include "auth_service.hpp"
#include "password_utils.hpp"

#include <sqlite3.h>
#include <sodium.h>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;


// Count successful and failed tests.
int passed = 0;
int failed = 0;


// Print test result.
void check(const string& testName, bool condition) {
    if (condition) {
        cout << "[PASS] " << testName << '\n';
        passed++;
    } else {
        cout << "[FAIL] " << testName << '\n';
        failed++;
    }
}


// Execute SQL statements used to initialize the test database.
void executeSQL(sqlite3* db, const char* sql) {
    char* errorMessage = nullptr;

    int rc = sqlite3_exec(
        db,
        sql,
        nullptr,
        nullptr,
        &errorMessage
    );

    if (rc != SQLITE_OK) {
        string error = errorMessage
            ? errorMessage
            : sqlite3_errmsg(db);

        sqlite3_free(errorMessage);

        throw runtime_error("SQLite error: " + error);
    }
}


// Insert a test user using a prepared statement.
void insertUser(
    sqlite3* db,
    int userID,
    const string& username,
    const string& passwordHash,
    int role,
    int isActive
) {
    const char* sql =
        "INSERT INTO users "
        "(user_id, username, password_hash, role, is_active) "
        "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(
        db,
        sql,
        -1,
        &stmt,
        nullptr
    );

    if (rc != SQLITE_OK) {
        throw runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_int(stmt, 1, userID);

    sqlite3_bind_text(
        stmt,
        2,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        3,
        passwordHash.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(stmt, 4, role);
    sqlite3_bind_int(stmt, 5, isActive);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);

        throw runtime_error(error);
    }

    sqlite3_finalize(stmt);
}


int main() {
    // STEP 1: Initialize libsodium.
    if (sodium_init() < 0) {
        cerr << "Failed to initialize libsodium\n";
        return 1;
    }

    // STEP 2: Create an in-memory SQLite database.
    sqlite3* db = nullptr;

    int rc = sqlite3_open(":memory:", &db);

    if (rc != SQLITE_OK) {
        cerr << "Failed to open test database\n";

        if (db != nullptr) {
            sqlite3_close(db);
        }

        return 1;
    }

    try {
        // STEP 3: Create a users table matching auth_service.cpp.
        executeSQL(db, R"(
            CREATE TABLE users (
                user_id INTEGER PRIMARY KEY,
                username TEXT NOT NULL UNIQUE,
                password_hash TEXT NOT NULL,
                role INTEGER NOT NULL
                    CHECK (role IN (1, 2, 3, 4)),
                is_active INTEGER NOT NULL
                    CHECK (is_active IN (0, 1))
            );
        )");

        // STEP 4: Generate password hashes for test users.
        string customerHash =
            hashPassword("CustomerPass123!");

        string employeeHash =
            hashPassword("EmployeePass123!");

        string inactiveHash =
            hashPassword("InactivePass123!");

        // STEP 5: Seed the test database.
        insertUser(
            db,
            101,
            "customer1",
            customerHash,
            1,  // Customer
            1   // Active
        );

        insertUser(
            db,
            102,
            "employee1",
            employeeHash,
            2,  // Employee
            1   // Active
        );

        insertUser(
            db,
            103,
            "inactive1",
            inactiveHash,
            1,  // Customer
            0   // Inactive
        );

        cout << "\n=== Authentication Tests ===\n\n";

        // STEP 6: Create the service using the test database.
        {
            authService auth(db);

            // TEST 1: Correct username, password, and role.
            authRes result = auth.authenticate(
                "customer1",
                "CustomerPass123!",
                1
            );

            check(
                "Valid customer login",
                result.success &&
                result.userID == 101 &&
                result.role == userRoles::Customer
            );


            // TEST 2: Incorrect password.
            result = auth.authenticate(
                "customer1",
                "WrongPassword123!",
                1
            );

            check(
                "Incorrect password is rejected",
                !result.success
            );


            // TEST 3: Username does not exist.
            result = auth.authenticate(
                "unknown_user",
                "SomePassword123!",
                1
            );

            check(
                "Nonexistent user is rejected",
                !result.success
            );


            // TEST 4: Incorrect role selected by client.
            result = auth.authenticate(
                "customer1",
                "CustomerPass123!",
                4  // Administrator
            );

            check(
                "Role mismatch is rejected",
                !result.success
            );


            // TEST 5: Inactive account.
            result = auth.authenticate(
                "inactive1",
                "InactivePass123!",
                1
            );

            check(
                "Inactive user is rejected",
                !result.success
            );


            // TEST 6: Employee login and role retrieval.
            result = auth.authenticate(
                "employee1",
                "EmployeePass123!",
                2
            );

            check(
                "Valid employee login",
                result.success &&
                result.userID == 102 &&
                result.role == userRoles::Employee
            );


            // TEST 7: Unsupported role value.
            result = auth.authenticate(
                "customer1",
                "CustomerPass123!",
                99
            );

            check(
                "Invalid role is rejected",
                !result.success
            );
        }

        // STEP 7: Display summary.
        cout << "\n=== Test Summary ===\n";
        cout << "Passed: " << passed << '\n';
        cout << "Failed: " << failed << '\n';

    } catch (const exception& e) {
        cerr << "Test setup/runtime error: "
             << e.what() << '\n';

        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);

    return failed == 0 ? 0 : 1;
}
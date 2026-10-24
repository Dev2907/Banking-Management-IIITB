#pragma once

#include "../src/server/includes/password_utils.hpp"

#include <sqlite3.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace testSupport {

struct Database {
    sqlite3* handle = nullptr;

    Database() {
        if (sqlite3_open(":memory:", &handle) != SQLITE_OK) {
            throw std::runtime_error(handle ? sqlite3_errmsg(handle) : "SQLite open failed");
        }
        exec("PRAGMA foreign_keys = ON;");
        exec(R"(
            CREATE TABLE users (
                user_id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT NOT NULL UNIQUE,
                password_hash NOT NULL,
                role INTEGER NOT NULL CHECK (role IN (1,2,3,4)),
                is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)),
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE accounts (
                account_id INTEGER PRIMARY KEY AUTOINCREMENT,
                customer_id INTEGER NOT NULL REFERENCES users(user_id),
                balance_cents INTEGER NOT NULL CHECK (balance_cents >= 0),
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
            CREATE TABLE transactions (
                transaction_id INTEGER PRIMARY KEY AUTOINCREMENT,
                account_id INTEGER NOT NULL REFERENCES accounts(account_id),
                amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
                type INTEGER NOT NULL CHECK (type IN (1,2)),
                reference_id INTEGER REFERENCES transactions(transaction_id),
                timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                balance_after_cents INTEGER NOT NULL CHECK (balance_after_cents > 0)
            );
            CREATE TABLE loans (
                loan_id INTEGER PRIMARY KEY AUTOINCREMENT,
                customer_id INTEGER NOT NULL REFERENCES users(user_id),
                account_id INTEGER NOT NULL REFERENCES accounts(account_id),
                amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
                status TEXT NOT NULL CHECK (status IN ('1','2','3','4','5')),
                assigned_employee_id INTEGER REFERENCES users(user_id),
                decision_by INTEGER REFERENCES users(user_id),
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                decision_at TEXT
            );
            CREATE TABLE feedback (
                feedback_id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL REFERENCES users(user_id),
                message TEXT NOT NULL,
                timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )");
    }

    ~Database() {
        if (handle) sqlite3_close(handle);
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const char* sql) {
        char* error = nullptr;
        int rc = sqlite3_exec(handle, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            std::string message = error ? error : sqlite3_errmsg(handle);
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

    void exec(const std::string& sql) {
        exec(sql.c_str());
    }

    int addUser(const std::string& username, const std::string& password, int role, bool active = true) {
        const std::string hash = hashPassword(password);
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(
            handle,
            "INSERT INTO users (username, password_hash, role, is_active) VALUES (?, ?, ?, ?);",
            -1,
            &stmt,
            nullptr
        );
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, role);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 4, active ? 1 : 0);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string message = sqlite3_errmsg(handle);
            sqlite3_finalize(stmt);
            throw std::runtime_error(message);
        }
        sqlite3_finalize(stmt);
        return static_cast<int>(sqlite3_last_insert_rowid(handle));
    }

    int addAccount(int userID, sqlite3_int64 balance) {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(
            handle,
            "INSERT INTO accounts (customer_id, balance_cents) VALUES (?, ?);",
            -1,
            &stmt,
            nullptr
        );
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 1, userID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 2, balance);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string message = sqlite3_errmsg(handle);
            sqlite3_finalize(stmt);
            throw std::runtime_error(message);
        }
        sqlite3_finalize(stmt);
        return static_cast<int>(sqlite3_last_insert_rowid(handle));
    }

    int addLoan(
        int customerID,
        int accountID,
        sqlite3_int64 amount,
        const std::string& status,
        int assignedEmployeeID = 0,
        int decisionByID = 0
    ) {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(
            handle,
            "INSERT INTO loans "
            "(customer_id, account_id, amount_cents, status, assigned_employee_id, decision_by) "
            "VALUES (?, ?, ?, ?, ?, ?);",
            -1,
            &stmt,
            nullptr
        );
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 1, customerID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, accountID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 3, amount);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, status.c_str(), -1, SQLITE_TRANSIENT);
        if (rc == SQLITE_OK && assignedEmployeeID == 0) rc = sqlite3_bind_null(stmt, 5);
        else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 5, assignedEmployeeID);
        if (rc == SQLITE_OK && decisionByID == 0) rc = sqlite3_bind_null(stmt, 6);
        else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, decisionByID);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string message = sqlite3_errmsg(handle);
            sqlite3_finalize(stmt);
            throw std::runtime_error(message);
        }
        sqlite3_finalize(stmt);
        return static_cast<int>(sqlite3_last_insert_rowid(handle));
    }

    sqlite3_int64 scalarInt64(
        const char* sql,
        int firstBindValue = 0,
        int secondBindValue = 0
    ) const {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK && firstBindValue != 0) {
            rc = sqlite3_bind_int(stmt, 1, firstBindValue);
        }
        if (rc == SQLITE_OK && secondBindValue != 0) {
            rc = sqlite3_bind_int(stmt, 2, secondBindValue);
        }
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            std::string message = sqlite3_errmsg(handle);
            sqlite3_finalize(stmt);
            throw std::runtime_error(message);
        }
        sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return value;
    }

    std::string textForUser(const char* sql, int userID) const {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 1, userID);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            std::string message = sqlite3_errmsg(handle);
            sqlite3_finalize(stmt);
            throw std::runtime_error(message);
        }
        const auto* value = sqlite3_column_text(stmt, 0);
        std::string result = value ? reinterpret_cast<const char*>(value) : "";
        sqlite3_finalize(stmt);
        return result;
    }
};

inline void check(bool condition, const std::string& name, int& passed, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        ++passed;
    } else {
        std::cout << "[FAIL] " << name << '\n';
        ++failed;
    }
}

inline int finish(int passed, int failed) {
    std::cout << "Passed: " << passed << ", failed: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}

} // namespace testSupport

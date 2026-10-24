#include "password_utils.hpp"

#include <sqlite3.h>
#include <sodium.h>

#include <array>
#include <exception>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

struct SeedUser {
    string username;
    string password;
    int role;
};

struct SeedTransaction {
    sqlite3_int64 amount;
    int type;
    string timestamp;
    sqlite3_int64 balanceAfter;
};

struct SeedLoan {
    size_t customerIndex;
    sqlite3_int64 amount;
    string status;
    int employeeIndex;
    int managerIndex;
    string createdAt;
    string decisionAt;
};

void check(int rc, sqlite3* db) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw runtime_error(sqlite3_errmsg(db));
    }
}

sqlite3_stmt* prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    check(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), db);
    return stmt;
}

int seedUser(sqlite3* db, const SeedUser& user) {
    const string passwordHash = hashPassword(user.password);
    const char* sql =
        "INSERT INTO users (username, password_hash, role, is_active) "
        "VALUES (?, ?, ?, 1) "
        "ON CONFLICT(username) DO UPDATE SET "
        "password_hash = excluded.password_hash, "
        "role = excluded.role, is_active = 1;";
    sqlite3_stmt* stmt = prepare(db, sql);

    int rc = sqlite3_bind_text(stmt, 1, user.username.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 3, user.role);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    sqlite3_finalize(stmt);

    stmt = prepare(db, "SELECT user_id FROM users WHERE username = ?;");
    rc = sqlite3_bind_text(stmt, 1, user.username.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_ROW) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    int userID = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return userID;
}

int ensureAccount(sqlite3* db, int customerID, sqlite3_int64 initialBalance) {
    const char* insertSQL =
        "INSERT INTO accounts (customer_id, balance_cents) "
        "SELECT ?, ? WHERE NOT EXISTS ("
        "SELECT 1 FROM accounts WHERE customer_id = ?);";
    sqlite3_stmt* stmt = prepare(db, insertSQL);
    int rc = sqlite3_bind_int(stmt, 1, customerID);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(stmt, 2, initialBalance);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 3, customerID);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    sqlite3_finalize(stmt);

    stmt = prepare(db,
        "SELECT account_id FROM accounts WHERE customer_id = ? ORDER BY account_id LIMIT 1;");
    rc = sqlite3_bind_int(stmt, 1, customerID);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_ROW) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    int accountID = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return accountID;
}

void seedTransaction(sqlite3* db, int accountID, const SeedTransaction& transaction) {
    const char* sql =
        "INSERT INTO transactions "
        "(account_id, amount_cents, type, reference_id, timestamp, balance_after_cents) "
        "SELECT ?, ?, ?, NULL, ?, ? WHERE NOT EXISTS ("
        "SELECT 1 FROM transactions WHERE account_id = ? AND amount_cents = ? "
        "AND type = ? AND timestamp = ?);";
    sqlite3_stmt* stmt = prepare(db, sql);
    int rc = sqlite3_bind_int(stmt, 1, accountID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 2, transaction.amount);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, transaction.type);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, transaction.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 5, transaction.balanceAfter);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, accountID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 7, transaction.amount);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 8, transaction.type);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 9, transaction.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    sqlite3_finalize(stmt);
}

void seedLoan(
    sqlite3* db,
    int customerID,
    int accountID,
    int assignedEmployeeID,
    int decisionByID,
    const SeedLoan& loan
) {
    const char* sql =
        "INSERT INTO loans "
        "(customer_id, account_id, amount_cents, status, assigned_employee_id, "
        "decision_by, created_at, decision_at) "
        "SELECT ?, ?, ?, ?, ?, ?, ?, ? WHERE NOT EXISTS ("
        "SELECT 1 FROM loans WHERE customer_id = ? AND account_id = ? "
        "AND amount_cents = ? AND status = ? AND assigned_employee_id IS ? "
        "AND decision_by IS ? AND created_at = ?);";
    sqlite3_stmt* stmt = prepare(db, sql);
    int rc = sqlite3_bind_int(stmt, 1, customerID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, accountID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 3, loan.amount);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, loan.status.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK && assignedEmployeeID < 0) rc = sqlite3_bind_null(stmt, 5);
    else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 5, assignedEmployeeID);
    if (rc == SQLITE_OK && decisionByID < 0) rc = sqlite3_bind_null(stmt, 6);
    else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, decisionByID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 7, loan.createdAt.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK && loan.decisionAt.empty()) rc = sqlite3_bind_null(stmt, 8);
    else if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 8, loan.decisionAt.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 9, customerID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 10, accountID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 11, loan.amount);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 12, loan.status.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK && assignedEmployeeID < 0) rc = sqlite3_bind_null(stmt, 13);
    else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 13, assignedEmployeeID);
    if (rc == SQLITE_OK && decisionByID < 0) rc = sqlite3_bind_null(stmt, 14);
    else if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 14, decisionByID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 15, loan.createdAt.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw runtime_error(error);
    }
    sqlite3_finalize(stmt);
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        cerr << "Failed to initialize libsodium\n";
        return 1;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open("src/db/main.db", &db) != SQLITE_OK) {
        cerr << "Database error: "
             << (db ? sqlite3_errmsg(db) : "Unable to open database") << '\n';
        if (db) sqlite3_close(db);
        return 1;
    }

    const array<SeedUser, 8> users{{
        {"customer1", "pass", 1},
        {"customer2", "pass", 1},
        {"customer3", "pass", 1},
        {"employee1", "pass", 2},
        {"employee2", "pass", 2},
        {"manager1", "pass", 3},
        {"manager2", "pass", 3},
        {"admin", "pass", 4}
    }};

    try {
        check(sqlite3_exec(db, "PRAGMA foreign_keys = ON; BEGIN IMMEDIATE;", nullptr, nullptr, nullptr), db);

        map<string, int> userIDs;
        for (const SeedUser& user : users) {
            userIDs[user.username] = seedUser(db, user);
        }

        const vector<string> customerNames = {"customer1", "customer2", "customer3"};
        vector<int> accountIDs;
        for (size_t index = 0; index < customerNames.size(); ++index) {
            sqlite3_int64 depositAmount = 50000 + static_cast<sqlite3_int64>(index) * 10000;
            sqlite3_int64 withdrawalAmount = 25000 + static_cast<sqlite3_int64>(index) * 5000;
            sqlite3_int64 finalBalance = 100000 + depositAmount - withdrawalAmount;
            int accountID = ensureAccount(db, userIDs.at(customerNames[index]), finalBalance);
            accountIDs.push_back(accountID);

            string depositTime = "2025-01-0" + to_string(index + 1) + " 09:00:00";
            string withdrawalTime = "2025-01-0" + to_string(index + 1) + " 10:00:00";
            seedTransaction(db, accountID, {depositAmount, 2, depositTime, 100000 + depositAmount});
            seedTransaction(db, accountID, {withdrawalAmount, 1, withdrawalTime, finalBalance});
        }

        const array<SeedLoan, 5> loans{{
            {0, 200000, "1", -1, -1, "2025-02-01 09:00:00", ""},
            {1, 300000, "2", 0, -1, "2025-02-02 09:00:00", ""},
            {2, 400000, "3", 1, 0, "2025-02-03 09:00:00", "2025-02-04 09:00:00"},
            {0, 150000, "4", 1, 1, "2025-02-05 09:00:00", "2025-02-06 09:00:00"},
            {1, 250000, "5", 0, 0, "2025-02-07 09:00:00", "2025-02-08 09:00:00"}
        }};
        for (const SeedLoan& loan : loans) {
            int employeeID = loan.employeeIndex < 0
                ? -1
                : userIDs.at("employee" + to_string(loan.employeeIndex + 1));
            int managerID = loan.managerIndex < 0
                ? -1
                : userIDs.at("manager" + to_string(loan.managerIndex + 1));
            seedLoan(
                db,
                userIDs.at(customerNames[loan.customerIndex]),
                accountIDs[loan.customerIndex],
                employeeID,
                managerID,
                loan
            );
        }

        check(sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr), db);
        cout << "Seed data created or updated. All seeded passwords are: pass\n"
             << "Customers: customer1, customer2, customer3\n"
             << "Employees: employee1, employee2\n"
             << "Managers: manager1, manager2\n"
             << "Administrator: admin\n";
    } catch (const exception& error) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        cerr << "Error: " << error.what() << '\n';
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    return 0;
}
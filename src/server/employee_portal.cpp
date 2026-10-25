#include "includes/employee_portal.hpp"
#include "includes/database_operations.hpp"
#include "includes/password_utils.hpp"

#include <limits>
#include <stdexcept>

using namespace std;

employeePortal::employeePortal(sqlite3* db, int employeeID)
    : db_(db), employeeID_(employeeID) {
    if (db_ == nullptr) {
        throw invalid_argument("DB connection cannot be null");
    }
    if (employeeID_ < 0) {
        throw invalid_argument("Employee ID is invalid");
    }
}

bool employeePortal::prepareStatement(
    const char* sql,
    sqlite3_stmt*& stmt
) const {
    return databaseOperations::prepareStatement(db_, sql, stmt);
}

employeeRes employeePortal::changeCustomerUsername(
    const string& currentUsername,
    const string& newUsername
) {
    employeeRes res{};
    if (currentUsername.empty() || currentUsername.size() > 64 ||
        newUsername.empty() || newUsername.size() > 64) {
        res.msg = "Usernames must contain 1 to 64 characters";
        return res;
    }

    const char* sql =
        "UPDATE users SET username = ? "
        "WHERE username = ? AND role = 1 AND EXISTS ("
        "SELECT 1 FROM users WHERE user_id = ? AND role = 2 AND is_active = 1);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_text(stmt, 1, newUsername.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 2, currentUsername.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 3, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }
    int rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rowsChanged != 1) {
        res.msg = "Customer not found or username is already in use";
        return res;
    }

    res.status = true;
    res.msg = "Customer username updated";
    return res;
}

employeeRes employeePortal::setCustomerActive(
    const string& username,
    bool isActive
) {
    employeeRes res{};
    if (username.empty() || username.size() > 64) {
        res.msg = "Invalid customer username";
        return res;
    }

    const char* sql =
        "UPDATE users SET is_active = ? "
        "WHERE username = ? AND role = 1 AND EXISTS ("
        "SELECT 1 FROM users WHERE user_id = ? AND role = 2 AND is_active = 1);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, isActive ? 1 : 0);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 3, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }
    int rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rowsChanged != 1) {
        res.msg = "Customer not found";
        return res;
    }

    res.status = true;
    res.msg = isActive ? "Customer activated" : "Customer deactivated";
    return res;
}

employeeRes employeePortal::getAssignedLoanApplications() {
    employeeRes res{};
    const char* sql =
        "SELECT l.loan_id, u.username, l.account_id, l.amount_cents, "
        "l.status, l.created_at "
        "FROM loans l JOIN users u ON u.user_id = l.customer_id "
        "WHERE l.assigned_employee_id = ? "
        "ORDER BY l.created_at DESC, l.loan_id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, employeeID_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!res.msg.empty()) {
            res.msg += '\n';
        }
        const auto* username = sqlite3_column_text(stmt, 1);
        const auto* createdAt = sqlite3_column_text(stmt, 5);
        res.msg += "Loan ID: " + std::to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Customer: ";
        res.msg += username ? reinterpret_cast<const char*>(username) : "unknown";
        res.msg += " | Account: " + std::to_string(sqlite3_column_int(stmt, 2));
        res.msg += " | Amount: " + std::to_string(sqlite3_column_int64(stmt, 3));
        res.msg += " | Status: ";
        res.msg += sqlite3_column_text(stmt, 4)
            ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))
            : "unknown";
        res.msg += " | Submitted: ";
        res.msg += createdAt ? reinterpret_cast<const char*>(createdAt) : "unknown";
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    if (res.msg.empty()) {
        res.msg = "No loan applications are assigned to you";
    }
    return res;
}

employeeRes employeePortal::getAllUsersAndAccounts() {
    employeeRes res{};
    const char* sql =
        "SELECT u.user_id, u.username, u.role, u.is_active, "
        "a.account_id, a.balance_cents "
        "FROM users u LEFT JOIN accounts a ON a.customer_id = u.user_id "
        "WHERE u.role != 3 AND EXISTS (SELECT 1 FROM users employee "
        "WHERE employee.user_id = ? AND employee.role = 2 AND employee.is_active = 1) "
        "ORDER BY u.user_id, a.account_id;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, employeeID_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!res.msg.empty()) {
            res.msg += '\n';
        }
        const auto* username = sqlite3_column_text(stmt, 1);
        int role = sqlite3_column_int(stmt, 2);
        res.msg += "User " + std::to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Username: ";
        res.msg += username ? reinterpret_cast<const char*>(username) : "unknown";
        res.msg += " | Role: " + std::to_string(role);
        res.msg += " | State: ";
        res.msg += sqlite3_column_int(stmt, 3) == 1 ? "active" : "inactive";
        res.msg += " | Account: ";
        if (sqlite3_column_type(stmt, 4) == SQLITE_NULL) {
            res.msg += "none";
        } else {
            res.msg += std::to_string(sqlite3_column_int(stmt, 4));
            res.msg += " | Balance: " + std::to_string(sqlite3_column_int64(stmt, 5));
        }
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    if (res.msg.empty()) {
        res.msg = "No users found";
    }
    return res;
}

employeeRes employeePortal::processLoanApplication(
    int loanID,
    int decisionStatus
) {
    employeeRes res{};
    if (loanID <= 0 || (decisionStatus != 3 && decisionStatus != 4)) {
        res.msg = "Provide a valid loan ID and choose approve (3) or reject (4)";
        return res;
    }

    int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    auto rollback = [&]() {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    };

    sqlite3_stmt* stmt = nullptr;

    if (decisionStatus == 3) {
        const char* loanSQL =
            "SELECT l.account_id, l.amount_cents, a.balance_cents "
            "FROM loans l JOIN accounts a ON a.account_id = l.account_id "
            "WHERE l.loan_id = ? AND l.assigned_employee_id = ? AND l.status = '2' "
            "AND EXISTS (SELECT 1 FROM users "
            "WHERE user_id = ? AND role = 2 AND is_active = 1);";
        if (!prepareStatement(loanSQL, stmt)) {
            res.msg = sqlite3_errmsg(db_);
            rollback();
            return res;
        }

        rc = sqlite3_bind_int(stmt, 1, loanID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, employeeID_);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, employeeID_);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            res.msg = rc == SQLITE_DONE
                ? "Loan not found, not assigned to you, or not in assigned status"
                : sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            rollback();
            return res;
        }

        int accountID = sqlite3_column_int(stmt, 0);
        sqlite3_int64 loanAmount = sqlite3_column_int64(stmt, 1);
        sqlite3_int64 currentBalance = sqlite3_column_int64(stmt, 2);
        sqlite3_finalize(stmt);

        if (currentBalance < 0 ||
            loanAmount > numeric_limits<sqlite3_int64>::max() - currentBalance) {
            res.msg = "Loan disbursement would exceed the supported balance";
            rollback();
            return res;
        }
        sqlite3_int64 newBalance = currentBalance + loanAmount;

        const char* creditSQL =
            "UPDATE accounts SET balance_cents = balance_cents + ? "
            "WHERE account_id = ? AND balance_cents = ?;";
        if (!prepareStatement(creditSQL, stmt)) {
            res.msg = sqlite3_errmsg(db_);
            rollback();
            return res;
        }
        rc = sqlite3_bind_int64(stmt, 1, loanAmount);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, accountID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 3, currentBalance);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        int rowsChanged = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE || rowsChanged != 1) {
            res.msg = rc == SQLITE_DONE
                ? "Failed to credit the loan account"
                : sqlite3_errmsg(db_);
            rollback();
            return res;
        }

        const char* transactionSQL =
            "INSERT INTO transactions "
            "(account_id, amount_cents, type, balance_after_cents, timestamp) "
            "VALUES (?, ?, 2, ?, CURRENT_TIMESTAMP);";
        if (!prepareStatement(transactionSQL, stmt)) {
            res.msg = sqlite3_errmsg(db_);
            rollback();
            return res;
        }
        rc = sqlite3_bind_int(stmt, 1, accountID);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 2, loanAmount);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 3, newBalance);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            res.msg = sqlite3_errmsg(db_);
            rollback();
            return res;
        }
    }

    const char* decisionSQL =
        "UPDATE loans SET status = ?, decision_by = ?, "
        "decision_at = CURRENT_TIMESTAMP "
        "WHERE loan_id = ? AND assigned_employee_id = ? AND status = '2' "
        "AND EXISTS (SELECT 1 FROM users "
        "WHERE user_id = ? AND role = 2 AND is_active = 1);";
    if (!prepareStatement(decisionSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollback();
        return res;
    }

    rc = sqlite3_bind_int(stmt, 1, decisionStatus);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 3, loanID);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 4, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 5, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        rollback();
        return res;
    }
    int rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);

    if (rowsChanged != 1) {
        res.msg = "Loan not found, not assigned to you, or not in assigned status";
        rollback();
        return res;
    }

    rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        rollback();
        return res;
    }

    res.status = true;
    res.msg = decisionStatus == 3
        ? "Loan application approved"
        : "Loan application rejected";
    return res;
}

employeeRes employeePortal::getCustomerTransactionHistory(
    const std::string& customerUsername
) {
    employeeRes res{};
    if (customerUsername.empty() || customerUsername.size() > 64) {
        res.msg = "Invalid customer username";
        return res;
    }

    const char* sql =
        "SELECT t.transaction_id, t.account_id, t.type, t.amount_cents, "
        "t.balance_after_cents, t.timestamp, t.reference_id "
        "FROM transactions t "
        "JOIN accounts a ON a.account_id = t.account_id "
        "JOIN users customer ON customer.user_id = a.customer_id "
        "WHERE customer.username = ? AND customer.role = 1 "
        "AND EXISTS (SELECT 1 FROM users employee "
        "WHERE employee.user_id = ? AND employee.role = 2 AND employee.is_active = 1) "
        "ORDER BY t.timestamp DESC, t.transaction_id DESC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_text(
        stmt, 1, customerUsername.c_str(), -1, SQLITE_TRANSIENT
    );
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, employeeID_);
    }
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!res.msg.empty()) {
            res.msg += '\n';
        }
        const auto* timestamp = sqlite3_column_text(stmt, 5);
        res.msg += "Transaction " + std::to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Account: " + std::to_string(sqlite3_column_int(stmt, 1));
        res.msg += " | Type: " + std::to_string(sqlite3_column_int(stmt, 2));
        res.msg += " | Amount: " + std::to_string(sqlite3_column_int64(stmt, 3));
        res.msg += " | Balance after: " + std::to_string(sqlite3_column_int64(stmt, 4));
        res.msg += " | Time: ";
        res.msg += timestamp ? reinterpret_cast<const char*>(timestamp) : "unknown";
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
            res.msg += " | Reference: ";
            res.msg += std::to_string(sqlite3_column_int64(stmt, 6));
        }
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    if (res.msg.empty()) {
        res.msg = "No transactions found for this customer";
    }
    return res;
}

employeeRes employeePortal::changePassword(
    const std::string& currentPassword,
    const std::string& newPassword
) {
    employeeRes res{};
    if (currentPassword.empty() || newPassword.empty() || newPassword.size() > 1024) {
        res.msg = "Invalid password";
        return res;
    }

    const char* selectSQL = "SELECT password_hash FROM users WHERE user_id = ? AND role = 2;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(selectSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }
    int rc = sqlite3_bind_int(stmt, 1, employeeID_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_ROW) {
        res.msg = rc == SQLITE_DONE ? "Employee not found" : sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    const auto* hashText = sqlite3_column_text(stmt, 0);
    std::string storedHash = hashText
        ? reinterpret_cast<const char*>(hashText)
        : "";
    sqlite3_finalize(stmt);
    if (!verifyPassword(storedHash, currentPassword)) {
        res.msg = "Current password is incorrect";
        return res;
    }

    std::string newHash;
    try {
        newHash = hashPassword(newPassword);
    } catch (const std::exception& error) {
        res.msg = error.what();
        return res;
    }

    const char* updateSQL =
        "UPDATE users SET password_hash = ? WHERE user_id = ? AND role = 2;";
    if (!prepareStatement(updateSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }
    rc = sqlite3_bind_text(stmt, 1, newHash.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, employeeID_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    bool updated = rc == SQLITE_DONE && sqlite3_changes(db_) == 1;
    sqlite3_finalize(stmt);
    if (!updated) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    res.msg = "Password changed successfully";
    return res;
}
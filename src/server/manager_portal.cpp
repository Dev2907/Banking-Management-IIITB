#include "includes/manager_portal.hpp"
#include "includes/database_operations.hpp"
#include "includes/password_utils.hpp"

#include <stdexcept>

using namespace std;

managerPortal::managerPortal(sqlite3* db, int managerID)
    : db_(db), managerID_(managerID) {
    if (db_ == nullptr) {
        throw invalid_argument("DB connection cannot be null");
    }
    if (managerID_ < 0) {
        throw invalid_argument("Manager ID is invalid");
    }
}

bool managerPortal::prepareStatement(
    const char* sql,
    sqlite3_stmt*& stmt
) const {
    return databaseOperations::prepareStatement(db_, sql, stmt);
}

managerRes managerPortal::changeCustomerUsername(
    const string& currentUsername,
    const string& newUsername
) {
    managerRes res{};
    if (currentUsername.empty() || currentUsername.size() > 64 ||
        newUsername.empty() || newUsername.size() > 64) {
        res.msg = "Usernames must contain 1 to 64 characters";
        return res;
    }

    const char* sql =
        "UPDATE users SET username = ? "
        "WHERE username = ? AND role = 1 AND EXISTS ("
        "SELECT 1 FROM users WHERE user_id = ? AND role = 3 AND is_active = 1);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_text(stmt, 1, newUsername.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, currentUsername.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, managerID_);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
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

managerRes managerPortal::setCustomerActive(
    const string& username,
    bool isActive
) {
    managerRes res{};
    if (username.empty() || username.size() > 64) {
        res.msg = "Invalid customer username";
        return res;
    }

    const char* sql =
        "UPDATE users SET is_active = ? "
        "WHERE username = ? AND role = 1 AND EXISTS ("
        "SELECT 1 FROM users WHERE user_id = ? AND role = 3 AND is_active = 1);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, isActive ? 1 : 0);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, managerID_);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
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

managerRes managerPortal::assignLoanApplication(
    const string& employeeUsername,
    int loanID
) {
    managerRes res{};
    if (employeeUsername.empty() || employeeUsername.size() > 64 || loanID <= 0) {
        res.msg = "Provide a valid employee username and loan ID";
        return res;
    }

    const char* sql =
        "UPDATE loans SET assigned_employee_id = ("
        "SELECT user_id FROM users WHERE username = ? AND role = 2 AND is_active = 1), "
        "status = '2' "
        "WHERE loan_id = ? AND status = '1' "
        "AND EXISTS (SELECT 1 FROM users employee "
        "WHERE employee.username = ? AND employee.role = 2 AND employee.is_active = 1) "
        "AND EXISTS (SELECT 1 FROM users manager "
        "WHERE manager.user_id = ? AND manager.role = 3 AND manager.is_active = 1);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_text(stmt, 1, employeeUsername.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, loanID);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, employeeUsername.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 4, managerID_);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    int rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    if (rowsChanged != 1) {
        res.msg = "Loan must be pending and employee must be active";
        return res;
    }

    res.status = true;
    res.msg = "Loan application assigned to employee";
    return res;
}

managerRes managerPortal::getLoanApplications() {
    managerRes res{};
    const char* sql =
        "SELECT l.loan_id, customer.username, l.account_id, l.amount_cents, "
        "l.status, employee.username, l.created_at, l.decision_at "
        "FROM loans l "
        "JOIN users customer ON customer.user_id = l.customer_id "
        "LEFT JOIN users employee ON employee.user_id = l.assigned_employee_id "
        "WHERE EXISTS (SELECT 1 FROM users manager "
        "WHERE manager.user_id = ? AND manager.role = 3 AND manager.is_active = 1) "
        "ORDER BY l.created_at DESC, l.loan_id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, managerID_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!res.msg.empty()) {
            res.msg += '\n';
        }
        const auto* customer = sqlite3_column_text(stmt, 1);
        const auto* employee = sqlite3_column_text(stmt, 5);
        const auto* createdAt = sqlite3_column_text(stmt, 6);
        const auto* decisionAt = sqlite3_column_text(stmt, 7);
        const auto* status = sqlite3_column_text(stmt, 4);

        res.msg += "Loan ID: " + to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Customer: ";
        res.msg += customer ? reinterpret_cast<const char*>(customer) : "unknown";
        res.msg += " | Account: " + to_string(sqlite3_column_int(stmt, 2));
        res.msg += " | Amount: " + to_string(sqlite3_column_int64(stmt, 3));
        res.msg += " | Status: ";
        res.msg += status ? reinterpret_cast<const char*>(status) : "unknown";
        res.msg += " | Assigned employee: ";
        res.msg += employee ? reinterpret_cast<const char*>(employee) : "unassigned";
        res.msg += " | Submitted: ";
        res.msg += createdAt ? reinterpret_cast<const char*>(createdAt) : "unknown";
        res.msg += " | Decision: ";
        res.msg += decisionAt ? reinterpret_cast<const char*>(decisionAt) : "pending";
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    if (res.msg.empty()) {
        res.msg = "No loan applications found";
    }
    return res;
}

managerRes managerPortal::getAllUsersAndAccounts() {
    managerRes res{};
    const char* sql =
        "SELECT u.user_id, u.username, u.role, u.is_active, "
        "a.account_id, a.balance_cents "
        "FROM users u LEFT JOIN accounts a ON a.customer_id = u.user_id "
        "WHERE u.role != 3 AND EXISTS (SELECT 1 FROM users manager "
        "WHERE manager.user_id = ? AND manager.role = 3 AND manager.is_active = 1) "
        "ORDER BY u.user_id, a.account_id;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, managerID_);
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
        res.msg += "User " + to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Username: ";
        res.msg += username ? reinterpret_cast<const char*>(username) : "unknown";
        res.msg += " | Role: " + to_string(role);
        res.msg += " | State: ";
        res.msg += sqlite3_column_int(stmt, 3) == 1 ? "active" : "inactive";
        res.msg += " | Account: ";
        if (sqlite3_column_type(stmt, 4) == SQLITE_NULL) {
            res.msg += "none";
        } else {
            res.msg += to_string(sqlite3_column_int(stmt, 4));
            res.msg += " | Balance: " + to_string(sqlite3_column_int64(stmt, 5));
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

managerRes managerPortal::getCustomerFeedback() {
    managerRes res{};
    const char* sql =
        "SELECT f.feedback_id, u.username, f.message, f.timestamp "
        "FROM feedback f JOIN users u ON u.user_id = f.user_id "
        "WHERE u.role = 1 AND EXISTS (SELECT 1 FROM users manager "
        "WHERE manager.user_id = ? AND manager.role = 3 AND manager.is_active = 1) "
        "ORDER BY f.timestamp DESC, f.feedback_id DESC LIMIT 100;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, managerID_);
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
        const auto* message = sqlite3_column_text(stmt, 2);
        const auto* timestamp = sqlite3_column_text(stmt, 3);
        res.msg += "Feedback ID: " + to_string(sqlite3_column_int(stmt, 0));
        res.msg += " | Customer: ";
        res.msg += username ? reinterpret_cast<const char*>(username) : "unknown";
        res.msg += " | Submitted: ";
        res.msg += timestamp ? reinterpret_cast<const char*>(timestamp) : "unknown";
        res.msg += " | Message: ";
        res.msg += message ? reinterpret_cast<const char*>(message) : "";
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    if (res.msg.empty()) {
        res.msg = "No customer feedback found";
    }
    return res;
}

managerRes managerPortal::changePassword(
    const string& currentPassword,
    const string& newPassword
) {
    managerRes res{};
    if (currentPassword.empty() || newPassword.empty() || newPassword.size() > 1024) {
        res.msg = "Invalid password";
        return res;
    }

    const char* selectSQL =
        "SELECT password_hash FROM users "
        "WHERE user_id = ? AND role = 3 AND is_active = 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(selectSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, managerID_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_ROW) {
        res.msg = rc == SQLITE_DONE ? "Active manager not found" : sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    const auto* hashText = sqlite3_column_text(stmt, 0);
    string storedHash = hashText
        ? reinterpret_cast<const char*>(hashText)
        : "";
    sqlite3_finalize(stmt);
    if (!verifyPassword(storedHash, currentPassword)) {
        res.msg = "Current password is incorrect";
        return res;
    }

    string newHash;
    try {
        newHash = hashPassword(newPassword);
    } catch (const exception& error) {
        res.msg = error.what();
        return res;
    }

    const char* updateSQL =
        "UPDATE users SET password_hash = ? "
        "WHERE user_id = ? AND role = 3 AND is_active = 1;";
    if (!prepareStatement(updateSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }
    rc = sqlite3_bind_text(stmt, 1, newHash.c_str(), -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, managerID_);
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
#include "includes/customer_portal.hpp"
#include "includes/password_utils.hpp"

#include <iostream>
#include <stdexcept>
#include <limits>


using namespace std;
customerPortal::customerPortal(sqlite3* db, int userId): db_(db),userId_(userId){
    if(db_ == nullptr){
        throw invalid_argument("DB connection cannot be null");
    }
    if(userId_ == -1){
        throw invalid_argument("User ID cannot be null");
    }
}

bool customerPortal::prepareStatement(const char* sql, sqlite3_stmt*& stmt) const{
    stmt = nullptr;
    return sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK;
}

bool customerPortal::beginTransaction(custRes& res) const{
    char* errorMessage = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, &errorMessage);

    if (rc != SQLITE_OK) {
        res.msg = errorMessage ? errorMessage : sqlite3_errmsg(db_);
        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool customerPortal::commitTransaction(custRes& res) const{
    int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return false;
    }

    return true;
}

void customerPortal::rollbackTransaction() const{
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
}

bool customerPortal::getAccount(
    int& accountID,
    sqlite3_int64& balance,
    custRes& res
) const {
    const char* sql =
        "SELECT account_id, balance_cents "
        "FROM accounts WHERE customer_id = ? AND account_id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (selectedAccountID_ < 0) {
        res.msg = "No account selected";
        return false;
    }

    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return false;
    }

    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, selectedAccountID_);
    }
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        res.msg = "No account found for this customer";
        sqlite3_finalize(stmt);
        return false;
    }
    if (rc != SQLITE_ROW) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    accountID = sqlite3_column_int(stmt, 0);
    balance = sqlite3_column_int64(stmt, 1);
    sqlite3_finalize(stmt);
    return true;
}

bool customerPortal::getAccounts(
    vector<customerAccount>& accounts,
    custRes& res
) const {
    accounts.clear();
    const char* sql =
        "SELECT account_id, balance_cents FROM accounts "
        "WHERE customer_id = ? ORDER BY account_id;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return false;
    }

    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        accounts.push_back({
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int64(stmt, 1)
        });
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        accounts.clear();
        return false;
    }

    return true;
}

bool customerPortal::listAccounts(
    vector<customerAccount>& accounts,
    custRes& res
) const {
    return getAccounts(accounts, res);
}

custRes customerPortal::selectAccount(int accountID) {
    custRes res{};
    const char* sql =
        "SELECT 1 FROM accounts WHERE account_id = ? AND customer_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, accountID);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, userId_);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_ROW) {
        res.msg = rc == SQLITE_DONE ? "Account does not belong to this customer" : sqlite3_errmsg(db_);
        return res;
    }

    selectedAccountID_ = accountID;
    res.status = true;
    res.msg = "Account selected";
    return res;
}

custRes customerPortal::getBalance(){
    custRes res;
    const char* sql =
        "SELECT balance_cents "
        "FROM accounts WHERE customer_id = ? AND account_id = ?;";

    sqlite3_stmt* stmt = nullptr;

    int rc;
    if(!prepareStatement(sql, stmt)){
        cerr << "SQLITE query error" << sqlite3_errmsg(db_) << endl;
        sqlite3_finalize(stmt);
        res.msg = "Query failed\n";
        return res;
    }

    rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc == SQLITE_OK && selectedAccountID_ >= 0) {
        rc = sqlite3_bind_int(stmt, 2, selectedAccountID_);
    }

    if(rc != SQLITE_OK || selectedAccountID_ < 0){
        cerr << "Sqlite bind error: " << sqlite3_errmsg(db_) << endl;
        sqlite3_finalize(stmt);
        res.msg = "Query failed\n";
        return res;
    }

    rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE) {
        // Username does not exist.
        sqlite3_finalize(stmt);

        res.msg = "Invalid userid";
        return res;
    }

    if (rc != SQLITE_ROW) {
        cerr << "SQLite query error: "
             << sqlite3_errmsg(db_) << '\n';

        sqlite3_finalize(stmt);

        res.msg = "Query failed";
        return res;
    }

    sqlite3_int64 balance = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    res.status = true;
    res.msg = to_string(balance);
    return res;
}

custRes customerPortal::createAccount() {
    custRes res{};
    const char* sql =
        "INSERT INTO accounts (customer_id, balance_cents) VALUES (?, 0);";
    sqlite3_stmt* stmt = nullptr;

    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    res.msg = "Account created. Account ID: " +
        to_string(sqlite3_last_insert_rowid(db_));
    return res;
}

custRes customerPortal::deposit(sqlite3_int64 amount){
    custRes res{};

    if(amount <= 0){
        res.msg = "Deposit amount must be greater than zero";
        return res;
    }

    int rc;
    if (!beginTransaction(res)) {
        return res;
    }

    int accountID;
    sqlite3_int64 currentBalance;
    if (!getAccount(accountID, currentBalance, res)) {
        rollbackTransaction();
        return res;
    }

    sqlite3_stmt* stmt = nullptr;

    // Prevent integer overflow.
    if (currentBalance < 0 ||
        amount > numeric_limits<sqlite3_int64>::max() - currentBalance) {

        rollbackTransaction();

        res.msg = "Deposit would exceed the supported balance";
        return res;
    }

    sqlite3_int64 newBalance = currentBalance + amount;

    // Update the account balance.
    const char* updateSQL =
        "UPDATE accounts "
        "SET balance_cents = balance_cents + ? "
        "WHERE account_id = ? "
        "AND customer_id = ?;";

    if (!prepareStatement(updateSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    sqlite3_bind_int64(stmt, 1, amount);
    sqlite3_bind_int(stmt, 2, accountID);
    sqlite3_bind_int(stmt, 3, userId_);

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        res.msg = "Failed to update account balance";
        rollbackTransaction();
        return res;
    }

    // Record the deposit
    const char* insertSQL =
        "INSERT INTO transactions "
        "(account_id, type, amount_cents, "
        "balance_after_cents, timestamp) "
        "VALUES (?, 2, ?, ?, CURRENT_TIMESTAMP);";

    if (!prepareStatement(insertSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    sqlite3_bind_int(stmt, 1, accountID);
    sqlite3_bind_int64(stmt, 2, amount);
    sqlite3_bind_int64(stmt, 3, newBalance);

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    if (!commitTransaction(res)) {
        return res;
    }

    res.status = true;

    // Return the new balance in paise.
    res.msg = to_string(newBalance);

    return res;
}

custRes customerPortal::withdraw(sqlite3_int64 amount) {
    custRes res{};

    // STEP 1: Validate the withdrawal amount.
    if (amount <= 0) {
        res.msg = "Withdrawal amount must be greater than zero";
        return res;
    }

    // STEP 2: Start a write transaction.
    int rc;
    if (!beginTransaction(res)) {
        return res;
    }

    // STEP 3: Find the customer's account.
    int accountID;
    sqlite3_int64 currentBalance;
    if (!getAccount(accountID, currentBalance, res)) {
        rollbackTransaction();
        return res;
    }

    sqlite3_stmt* stmt = nullptr;

    // STEP 4: Check whether the balance is sufficient.
    if (currentBalance < 0) {
        rollbackTransaction();

        res.msg = "Invalid account balance";
        return res;
    }

    if (currentBalance < amount) {
        rollbackTransaction();

        res.msg = "Insufficient funds";
        return res;
    }

    // Calculate the new balance.
    sqlite3_int64 newBalance = currentBalance - amount;

    // STEP 5: Update the account balance.
    const char* updateSQL =
        "UPDATE accounts "
        "SET balance_cents = balance_cents - ? "
        "WHERE account_id = ? "
        "AND customer_id = ? "
        "AND balance_cents >= ?;";

    if (!prepareStatement(updateSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    sqlite3_bind_int64(stmt, 1, amount);
    sqlite3_bind_int(stmt, 2, accountID);
    sqlite3_bind_int(stmt, 3, userId_);
    sqlite3_bind_int64(stmt, 4, amount);

    rc = sqlite3_step(stmt);

    int rowsChanged = sqlite3_changes(db_);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE || rowsChanged != 1) {
        rollbackTransaction();

        res.msg = "Withdrawal failed";
        return res;
    }

    // STEP 6: Record the withdrawal.
    const char* insertSQL =
        "INSERT INTO transactions "
        "(account_id, type, amount_cents, "
        "balance_after_cents, timestamp) "
        "VALUES (?, 1, ?, ?, CURRENT_TIMESTAMP);";

    if (!prepareStatement(insertSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    sqlite3_bind_int(stmt, 1, accountID);
    sqlite3_bind_int64(stmt, 2, amount);
    sqlite3_bind_int64(stmt, 3, newBalance);

    rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    // STEP 7: Commit both changes.
    if (!commitTransaction(res)) {
        return res;
    }

    // STEP 8: Return success.
    res.status = true;
    res.msg = to_string(newBalance);

    return res;
}

custRes customerPortal::transfer(
    const string& recipientUsername,
    sqlite3_int64 amount
) {
    custRes res{};
    if (amount <= 0 || recipientUsername.empty()) {
        res.msg = "Recipient username and positive transfer amount are required";
        return res;
    }

    if (!beginTransaction(res)) {
        return res;
    }

    int sourceAccountID;
    sqlite3_int64 sourceBalance;
    if (!getAccount(sourceAccountID, sourceBalance, res)) {
        rollbackTransaction();
        return res;
    }
    if (sourceBalance < amount) {
        res.msg = "Insufficient funds";
        rollbackTransaction();
        return res;
    }

    const char* recipientSQL =
        "SELECT a.account_id, a.balance_cents "
        "FROM accounts a JOIN users u ON u.user_id = a.customer_id "
        "WHERE u.username = ? AND u.role = 1 AND u.is_active = 1 "
        "ORDER BY a.account_id LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(recipientSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    int rc = sqlite3_bind_text(
        stmt,
        1,
        recipientUsername.c_str(),
        static_cast<int>(recipientUsername.size()),
        SQLITE_TRANSIENT
    );
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        rollbackTransaction();
        return res;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        res.msg = rc == SQLITE_DONE ? "Recipient account not found" : sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        rollbackTransaction();
        return res;
    }

    int recipientAccountID = sqlite3_column_int(stmt, 0);
    sqlite3_int64 recipientBalance = sqlite3_column_int64(stmt, 1);
    sqlite3_finalize(stmt);

    if (recipientAccountID == sourceAccountID) {
        res.msg = "Cannot transfer to the same account";
        rollbackTransaction();
        return res;
    }
    if (recipientBalance < 0 ||
        amount > numeric_limits<sqlite3_int64>::max() - recipientBalance) {
        res.msg = "Transfer would exceed the supported balance";
        rollbackTransaction();
        return res;
    }

    const char* debitSQL =
        "UPDATE accounts SET balance_cents = balance_cents - ? "
        "WHERE account_id = ? AND balance_cents >= ?;";
    if (!prepareStatement(debitSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_bind_int64(stmt, 1, amount);
    sqlite3_bind_int(stmt, 2, sourceAccountID);
    sqlite3_bind_int64(stmt, 3, amount);
    rc = sqlite3_step(stmt);
    int rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || rowsChanged != 1) {
        res.msg = "Transfer failed to debit source account";
        rollbackTransaction();
        return res;
    }

    const char* creditSQL =
        "UPDATE accounts SET balance_cents = balance_cents + ? "
        "WHERE account_id = ?;";
    if (!prepareStatement(creditSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_bind_int64(stmt, 1, amount);
    sqlite3_bind_int(stmt, 2, recipientAccountID);
    rc = sqlite3_step(stmt);
    rowsChanged = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || rowsChanged != 1) {
        res.msg = "Transfer failed to credit recipient account";
        rollbackTransaction();
        return res;
    }

    const char* transactionSQL =
        "INSERT INTO transactions "
        "(account_id, amount_cents, type, balance_after_cents) "
        "VALUES (?, ?, ?, ?);";
    if (!prepareStatement(transactionSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_bind_int(stmt, 1, sourceAccountID);
    sqlite3_bind_int64(stmt, 2, amount);
    sqlite3_bind_int(stmt, 3, 1);
    sqlite3_bind_int64(stmt, 4, sourceBalance - amount);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_int64 debitTransactionID = sqlite3_last_insert_rowid(db_);

    if (!prepareStatement(transactionSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_bind_int(stmt, 1, recipientAccountID);
    sqlite3_bind_int64(stmt, 2, amount);
    sqlite3_bind_int(stmt, 3, 2);
    sqlite3_bind_int64(stmt, 4, recipientBalance + amount);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_int64 creditTransactionID = sqlite3_last_insert_rowid(db_);

    const char* referenceSQL = "UPDATE transactions SET reference_id = ? WHERE transaction_id = ?;";
    if (!prepareStatement(referenceSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    sqlite3_bind_int64(stmt, 1, creditTransactionID);
    sqlite3_bind_int64(stmt, 2, debitTransactionID);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    if (!commitTransaction(res)) {
        return res;
    }

    res.status = true;
    res.msg = to_string(sourceBalance - amount);
    return res;
}

custRes customerPortal::getTransactionHistory() {
    custRes res{};
    if (selectedAccountID_ < 0) {
        res.msg = "No account selected";
        return res;
    }

    const char* sql =
        "SELECT t.transaction_id, t.type, t.amount_cents, "
        "t.balance_after_cents, t.timestamp, t.reference_id "
        "FROM transactions t "
        "WHERE t.account_id = ? "
        "ORDER BY t.timestamp DESC, t.transaction_id DESC LIMIT 10;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, selectedAccountID_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    string history;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* timestamp = sqlite3_column_text(stmt, 4);
        if (!history.empty()) {
            history += '\n';
        }
        history += "ID " + to_string(sqlite3_column_int(stmt, 0));
        history += ", Type " + to_string(sqlite3_column_int(stmt, 1));
        history += ", Amount " + to_string(sqlite3_column_int64(stmt, 2));
        history += ", Balance after " + to_string(sqlite3_column_int64(stmt, 3));
        history += ", Time ";
        history += timestamp
            ? reinterpret_cast<const char*>(timestamp)
            : "unknown";
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            history += ", Reference " + to_string(sqlite3_column_int64(stmt, 5));
        }
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    res.msg = history.empty() ? "No transactions for this account" : history;
    return res;
}

custRes customerPortal::applyForLoan(sqlite3_int64 amount) {
    custRes res{};
    if (amount <= 0) {
        res.msg = "Loan amount must be greater than zero";
        return res;
    }

    if (!beginTransaction(res)) {
        return res;
    }

    int accountID;
    sqlite3_int64 balance;
    if (!getAccount(accountID, balance, res)) {
        rollbackTransaction();
        return res;
    }
    (void)balance;

    const char* sql =
        "INSERT INTO loans "
        "(customer_id, account_id, amount_cents, status) "
        "SELECT ?, ?, ?, '1' FROM users "
        "WHERE role = 2 AND is_active = 1 ORDER BY user_id LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }

    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int(stmt, 2, accountID);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_int64(stmt, 3, amount);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        rollbackTransaction();
        return res;
    }
    if (sqlite3_changes(db_) != 1) {
        res.msg = "No active employee is available to assign this loan";
        rollbackTransaction();
        return res;
    }

    if (!commitTransaction(res)) {
        return res;
    }

    res.status = true;
    res.msg = "Loan application submitted with pending status";
    return res;
}

custRes customerPortal::changePassword(
    const string& currentPassword,
    const string& newPassword
) {
    custRes res{};
    if (currentPassword.empty() || newPassword.empty() || newPassword.size() > 1024) {
        res.msg = "Invalid password";
        return res;
    }

    const char* selectSQL = "SELECT password_hash FROM users WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(selectSQL, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }
    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc != SQLITE_OK) {
        res.msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        res.msg = rc == SQLITE_DONE ? "User not found" : sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return res;
    }

    const unsigned char* hashText = sqlite3_column_text(stmt, 0);
    string storedHash = hashText ? reinterpret_cast<const char*>(hashText) : "";
    sqlite3_finalize(stmt);
    if (!verifyPassword(storedHash, currentPassword)) {
        res.msg = "Current password is incorrect";
        return res;
    }

    try {
        string newHash = hashPassword(newPassword);
        const char* updateSQL = "UPDATE users SET password_hash = ? WHERE user_id = ?;";
        if (!prepareStatement(updateSQL, stmt)) {
            res.msg = sqlite3_errmsg(db_);
            return res;
        }
        rc = sqlite3_bind_text(stmt, 1, newHash.c_str(), -1, SQLITE_TRANSIENT);
        if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, userId_);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        bool updated = rc == SQLITE_DONE && sqlite3_changes(db_) == 1;
        sqlite3_finalize(stmt);
        if (!updated) {
            res.msg = sqlite3_errmsg(db_);
            return res;
        }
    } catch (const exception& error) {
        res.msg = error.what();
        return res;
    }

    res.status = true;
    res.msg = "Password changed successfully";
    return res;
}

custRes customerPortal::submitFeedback(const string& message) {
    custRes res{};
    if (message.empty() || message.size() > 2000) {
        res.msg = "Feedback must contain 1 to 2000 characters";
        return res;
    }

    const char* sql = "INSERT INTO feedback (user_id, message) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(sql, stmt)) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }
    int rc = sqlite3_bind_int(stmt, 1, userId_);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(stmt, 2, message.c_str(), static_cast<int>(message.size()), SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        res.msg = sqlite3_errmsg(db_);
        return res;
    }

    res.status = true;
    res.msg = "Feedback submitted successfully";
    return res;
}


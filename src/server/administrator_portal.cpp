#include "includes/administrator_portal.hpp"
#include "includes/database_operations.hpp"
#include "includes/password_utils.hpp"

#include <stdexcept>

using namespace std;
administratorPortal::administratorPortal(sqlite3* db, int administratorID)
	: db_(db), administratorID_(administratorID) {
	if (db_ == nullptr) {
		throw invalid_argument("DB connection cannot be null");
	}
	if (administratorID_ < 0) {
		throw invalid_argument("Administrator ID is invalid");
	}
}

bool administratorPortal::prepareStatement(
	const char* sql,
	sqlite3_stmt*& stmt
) const {
	return databaseOperations::prepareStatement(db_, sql, stmt);
}

void administratorPortal::rollback() const {
	sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
}

administratorRes administratorPortal::createUser(
	const string& username,
	const string& password,
	int role
) {
	administratorRes res{};
	if (username.empty() || username.size() > 64 || password.empty() ||
		password.size() > 1024 || role < 1 || role > 3) {
		res.msg = "Provide a valid username, password, and role 1-3";
		return res;
	}

	string passwordHash;
	try {
		passwordHash = hashPassword(password);
	} catch (const exception& error) {
		res.msg = error.what();
		return res;
	}

	int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		res.msg = sqlite3_errmsg(db_);
		return res;
	}

	sqlite3_stmt* stmt = nullptr;
	const char* adminSQL =
		"SELECT 1 FROM users WHERE user_id = ? AND role = 4 AND is_active = 1;";
	if (!prepareStatement(adminSQL, stmt)) {
		res.msg = sqlite3_errmsg(db_);
		rollback();
		return res;
	}
	rc = sqlite3_bind_int(stmt, 1, administratorID_);
	if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
	bool authorized = rc == SQLITE_ROW;
	sqlite3_finalize(stmt);
	if (!authorized) {
		res.msg = "Active administrator not found";
		rollback();
		return res;
	}

	const char* userSQL =
		"INSERT INTO users (username, password_hash, role, is_active) "
		"VALUES (?, ?, ?, 1);";
	if (!prepareStatement(userSQL, stmt)) {
		res.msg = sqlite3_errmsg(db_);
		rollback();
		return res;
	}
	rc = sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
	if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, role);
	if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		res.msg = sqlite3_errmsg(db_);
		sqlite3_finalize(stmt);
		rollback();
		return res;
	}
	int newUserID = static_cast<int>(sqlite3_last_insert_rowid(db_));
	sqlite3_finalize(stmt);

	if (role == 1) {
		const char* accountSQL =
			"INSERT INTO accounts (customer_id, balance_cents) VALUES (?, 0);";
		if (!prepareStatement(accountSQL, stmt)) {
			res.msg = sqlite3_errmsg(db_);
			rollback();
			return res;
		}
		rc = sqlite3_bind_int(stmt, 1, newUserID);
		if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			res.msg = sqlite3_errmsg(db_);
			sqlite3_finalize(stmt);
			rollback();
			return res;
		}
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		res.msg = sqlite3_errmsg(db_);
		rollback();
		return res;
	}

	res.status = true;
	res.msg = role == 1 ? "Customer created with account" : "User created successfully";
	return res;
}

administratorRes administratorPortal::modifyUser(
	const string& currentUsername,
	const string& newUsername,
	int newRole,
	bool isActive
) {
	administratorRes res{};
	if (currentUsername.empty() || currentUsername.size() > 64 ||
		newUsername.empty() || newUsername.size() > 64 || newRole < 1 || newRole > 4) {
		res.msg = "Provide valid usernames and role 1-4";
		return res;
	}

	int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		res.msg = sqlite3_errmsg(db_);
		return res;
	}

	sqlite3_stmt* stmt = nullptr;
	const char* updateSQL =
		"UPDATE users SET username = ?, role = ?, is_active = ? "
		"WHERE username = ? AND user_id != ? AND EXISTS ("
		"SELECT 1 FROM users admin WHERE admin.user_id = ? "
		"AND admin.role = 4 AND admin.is_active = 1);";
	if (!prepareStatement(updateSQL, stmt)) {
		res.msg = sqlite3_errmsg(db_);
		rollback();
		return res;
	}
	rc = sqlite3_bind_text(stmt, 1, newUsername.c_str(), -1, SQLITE_TRANSIENT);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, newRole);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, isActive ? 1 : 0);
	if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, currentUsername.c_str(), -1, SQLITE_TRANSIENT);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 5, administratorID_);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, administratorID_);
	if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		res.msg = sqlite3_errmsg(db_);
		sqlite3_finalize(stmt);
		rollback();
		return res;
	}
	int rowsChanged = sqlite3_changes(db_);
	sqlite3_finalize(stmt);
	if (rowsChanged != 1) {
		res.msg = "User not found or username is already in use";
		rollback();
		return res;
	}

	if (newRole == 1) {
		const char* accountSQL =
			"INSERT INTO accounts (customer_id, balance_cents) "
			"SELECT user_id, 0 FROM users WHERE username = ? "
			"AND NOT EXISTS (SELECT 1 FROM accounts WHERE customer_id = users.user_id);";
		if (!prepareStatement(accountSQL, stmt)) {
			res.msg = sqlite3_errmsg(db_);
			rollback();
			return res;
		}
		rc = sqlite3_bind_text(stmt, 1, newUsername.c_str(), -1, SQLITE_TRANSIENT);
		if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			res.msg = sqlite3_errmsg(db_);
			sqlite3_finalize(stmt);
			rollback();
			return res;
		}
		sqlite3_finalize(stmt);
	}

	rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		res.msg = sqlite3_errmsg(db_);
		rollback();
		return res;
	}

	res.status = true;
	res.msg = "User details updated";
	return res;
}

administratorRes administratorPortal::changePassword(
	const string& currentPassword,
	const string& newPassword
) {
	administratorRes res{};
	if (currentPassword.empty() || newPassword.empty() || newPassword.size() > 1024) {
		res.msg = "Invalid password";
		return res;
	}

	sqlite3_stmt* stmt = nullptr;
	const char* selectSQL =
		"SELECT password_hash FROM users "
		"WHERE user_id = ? AND role = 4 AND is_active = 1;";
	if (!prepareStatement(selectSQL, stmt)) {
		res.msg = sqlite3_errmsg(db_);
		return res;
	}
	int rc = sqlite3_bind_int(stmt, 1, administratorID_);
	if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		res.msg = rc == SQLITE_DONE ? "Active administrator not found" : sqlite3_errmsg(db_);
		sqlite3_finalize(stmt);
		return res;
	}
	const auto* hashText = sqlite3_column_text(stmt, 0);
	string storedHash = hashText ? reinterpret_cast<const char*>(hashText) : "";
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
		"WHERE user_id = ? AND role = 4 AND is_active = 1;";
	if (!prepareStatement(updateSQL, stmt)) {
		res.msg = sqlite3_errmsg(db_);
		return res;
	}
	rc = sqlite3_bind_text(stmt, 1, newHash.c_str(), -1, SQLITE_TRANSIENT);
	if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 2, administratorID_);
	if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
	bool updated = rc == SQLITE_DONE && sqlite3_changes(db_) == 1;
	sqlite3_finalize(stmt);
	if (!updated) {
		res.msg = sqlite3_errmsg(db_);
		return res;
	}

	res.status = true;
	res.msg = "Administrator password changed successfully";
	return res;
}

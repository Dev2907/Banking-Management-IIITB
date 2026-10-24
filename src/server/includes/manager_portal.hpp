#pragma once

#include <sqlite3.h>
#include <string>

struct managerRes {
    bool status = false;
    std::string msg;
};

class managerPortal {
private:
    sqlite3* db_;
    int managerID_;

    bool prepareStatement(const char* sql, sqlite3_stmt*& stmt) const;

public:
    explicit managerPortal(sqlite3* db, int managerID);

    managerRes changeCustomerUsername(
        const std::string& currentUsername,
        const std::string& newUsername
    );
    managerRes setCustomerActive(const std::string& username, bool isActive);
    managerRes assignLoanApplication(
        const std::string& employeeUsername,
        int loanID
    );
    managerRes getLoanApplications();
    managerRes getAllUsersAndAccounts();
    managerRes getCustomerFeedback();
    managerRes changePassword(
        const std::string& currentPassword,
        const std::string& newPassword
    );
};
#pragma once

#include <sqlite3.h>
#include <string>

using namespace std;
struct employeeRes {
    bool status = false;
    string msg;
};

class employeePortal {
private:
    sqlite3* db_;
    int employeeID_;

    bool prepareStatement(const char* sql, sqlite3_stmt*& stmt) const;

public:
    explicit employeePortal(sqlite3* db, int employeeID);

    employeeRes changeCustomerUsername(
        const string& currentUsername,
        const string& newUsername
    );
    employeeRes setCustomerActive(
        const string& username,
        bool isActive
    );
    employeeRes getAssignedLoanApplications();
    employeeRes getAllUsersAndAccounts();
    employeeRes processLoanApplication(int loanID, int decisionStatus);
    employeeRes getCustomerTransactionHistory(const string& customerUsername);
    employeeRes changePassword(
        const string& currentPassword,
        const string& newPassword
    );
};
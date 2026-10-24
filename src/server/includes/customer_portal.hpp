#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

using namespace std;
struct custRes{
    bool status = false;
    string msg;
};

struct customerAccount {
    int accountID;
    sqlite3_int64 balance;
};

class customerPortal{
    private:
        sqlite3* db_;
        int userId_ = -1;
        int selectedAccountID_ = -1;

        bool prepareStatement(const char* sql, sqlite3_stmt*& stmt) const;
        bool beginTransaction(custRes& res) const;
        bool commitTransaction(custRes& res) const;
        void rollbackTransaction() const;
        bool getAccounts(vector<customerAccount>& accounts, custRes& res) const;
        bool getAccount(int& accountID, sqlite3_int64& balance, custRes& res) const;
    
    public:
        explicit customerPortal(sqlite3* db, int userId);

        bool listAccounts(vector<customerAccount>& accounts, custRes& res) const;
        custRes selectAccount(int accountID);
        custRes getBalance();
        custRes createAccount();
        custRes deposit(sqlite3_int64 amount);
        custRes withdraw(sqlite3_int64 amount);
        custRes transfer(const string& recipientUsername, sqlite3_int64 amount);
        custRes getTransactionHistory();
        custRes applyForLoan(sqlite3_int64 amount);
        custRes changePassword(const string& currentPassword, const string& newPassword);
        custRes submitFeedback(const string& message);
};
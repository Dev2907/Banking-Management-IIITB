#include "test_support.hpp"
#include "../src/server/includes/customer_portal.hpp"

using namespace testSupport;

int main() {
    int passed = 0;
    int failed = 0;
    try {
        Database database;
                        int customerID = database.addUser("customer", "old-pass", 1);
                        int customerAccountID = database.addAccount(customerID, 200000);
        int recipientID = database.addUser("recipient", "recipient-pass", 1);
        int recipientAccount = database.addAccount(recipientID, 10000);
        database.addUser("employee", "employee-pass", 2);

        customerPortal customer(database.handle, customerID);
        std::vector<customerAccount> accounts;
        custRes result;
        check(customer.listAccounts(accounts, result) && accounts.size() == 1,
              "customer lists owned accounts", passed, failed);
                        check(customer.selectAccount(customerAccountID).status,
              "customer selects owned account", passed, failed);
        check(!customer.selectAccount(recipientAccount).status,
              "customer cannot select another user's account", passed, failed);
                        check(customer.selectAccount(customerAccountID).status,
              "customer reselects owned account", passed, failed);

        result = customer.getBalance();
        check(result.status && result.msg == "200000", "view selected account balance", passed, failed);
        result = customer.deposit(50000);
        check(result.status && result.msg == "250000", "deposit updates balance", passed, failed);
                        check(database.scalarInt64("SELECT COUNT(*) FROM transactions WHERE account_id = ?", customerAccountID) == 1,
              "deposit writes transaction history", passed, failed);

        result = customer.withdraw(40000);
        check(result.status && result.msg == "210000", "withdrawal updates balance", passed, failed);
        result = customer.withdraw(300000);
      check(!result.status && database.scalarInt64("SELECT balance_cents FROM accounts WHERE account_id = ?", customerAccountID) == 210000,
              "insufficient withdrawal leaves balance unchanged", passed, failed);

        result = customer.transfer("recipient", 25000);
        check(result.status && result.msg == "185000", "transfer debits selected source", passed, failed);
        check(database.scalarInt64("SELECT balance_cents FROM accounts WHERE account_id = ?", recipientAccount) == 35000,
              "transfer credits recipient account", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM transactions WHERE reference_id IS NOT NULL") == 1,
              "transfer records linked transaction pair", passed, failed);

        result = customer.getTransactionHistory();
        check(result.status && result.msg.find("Amount") != std::string::npos,
              "view transaction history", passed, failed);

        result = customer.createAccount();
        check(result.status, "create additional account", passed, failed);
        check(customer.getBalance().msg == "185000",
              "new account creation keeps current account selected", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM accounts WHERE customer_id = ?", customerID) == 2,
              "additional account belongs to customer", passed, failed);
        result = customer.applyForLoan(90000);
        check(result.status, "apply for loan", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM loans WHERE customer_id = ? AND status = '1'", customerID) == 1,
              "loan application begins pending", passed, failed);
        check(customer.submitFeedback("Helpful service").status,
              "submit feedback", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM feedback WHERE user_id = ?", customerID) == 1,
              "feedback persisted", passed, failed);

        check(customer.changePassword("old-pass", "new-pass").status,
              "change password with valid current password", passed, failed);
        check(!customer.changePassword("old-pass", "third-pass").status,
              "reject incorrect current password", passed, failed);
        check(verifyPassword(database.textForUser("SELECT password_hash FROM users WHERE user_id = ?", customerID), "new-pass"),
              "new customer password hash authenticates", passed, failed);
    } catch (const std::exception& error) {
        std::cerr << "Test setup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return finish(passed, failed);
}

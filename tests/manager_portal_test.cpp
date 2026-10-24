#include "test_support.hpp"
#include "../src/server/includes/manager_portal.hpp"

using namespace testSupport;

int main() {
    int passed = 0;
    int failed = 0;
    try {
        Database database;
        int managerID = database.addUser("manager", "manager-old", 3);
        int inactiveManagerID = database.addUser("inactive-manager", "inactive-pass", 3, false);
        int customerID = database.addUser("customer", "customer-pass", 1);
        int customerAccount = database.addAccount(customerID, 100000);
        int employeeID = database.addUser("employee", "employee-pass", 2);
      database.addUser("inactive-employee", "inactive-employee-pass", 2, false);
        int otherManagerID = database.addUser("other-manager", "other-manager-pass", 3);
        database.addAccount(otherManagerID, 70000);
        int loanID = database.addLoan(customerID, customerAccount, 50000, "1");
        int alreadyAssignedLoan = database.addLoan(customerID, customerAccount, 60000, "2", employeeID);
        database.addLoan(customerID, customerAccount, 70000, "1");

        managerPortal manager(database.handle, managerID);
        managerRes result = manager.changeCustomerUsername("customer", "customer-renamed");
        check(result.status, "manager changes customer username", passed, failed);
        check(!manager.changeCustomerUsername("employee", "employee-renamed").status,
              "manager cannot change employee username", passed, failed);
        check(!manager.setCustomerActive("employee", false).status,
              "manager cannot change employee active status", passed, failed);
        check(manager.setCustomerActive("customer-renamed", false).status,
              "manager deactivates customer", passed, failed);
        check(database.scalarInt64("SELECT is_active FROM users WHERE user_id = ?", customerID) == 0,
              "manager customer active state persisted", passed, failed);

        check(manager.assignLoanApplication("employee", loanID).status,
              "manager assigns pending loan to active employee", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM loans WHERE loan_id = ? AND status = '2' AND assigned_employee_id = ?", loanID, employeeID) == 1,
              "assignment sets employee and assigned state", passed, failed);
        check(!manager.assignLoanApplication("employee", alreadyAssignedLoan).status,
              "manager cannot reassign an assigned loan", passed, failed);
        check(!manager.assignLoanApplication("inactive-employee", loanID).status,
              "manager cannot assign loan to inactive employee", passed, failed);

        result = manager.getLoanApplications();
        check(result.status && result.msg.find("Loan ID: " + std::to_string(loanID)) != std::string::npos,
              "manager views loan applications", passed, failed);
        result = manager.getAllUsersAndAccounts();
        check(result.status && result.msg.find("customer-renamed") != std::string::npos,
              "manager views users and accounts", passed, failed);
        check(result.msg.find("other-manager") == std::string::npos,
              "manager listing hides other managers", passed, failed);

        database.exec("INSERT INTO feedback (user_id, message) VALUES (" + std::to_string(customerID) + ", 'Needs help');");
        result = manager.getCustomerFeedback();
        check(result.status && result.msg.find("Needs help") != std::string::npos,
              "manager reviews customer feedback", passed, failed);

        check(manager.changePassword("manager-old", "manager-new").status,
              "manager changes password", passed, failed);
        check(verifyPassword(database.textForUser("SELECT password_hash FROM users WHERE user_id = ?", managerID), "manager-new"),
              "new manager password hash verifies", passed, failed);

        managerPortal inactiveManager(database.handle, inactiveManagerID);
        result = inactiveManager.getLoanApplications();
        check(result.status && result.msg == "No loan applications found",
              "inactive manager cannot view loan applications", passed, failed);
    } catch (const std::exception& error) {
        std::cerr << "Test setup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return finish(passed, failed);
}

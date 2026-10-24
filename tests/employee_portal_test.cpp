#include "test_support.hpp"
#include "../src/server/includes/employee_portal.hpp"

using namespace testSupport;

int main() {
    int passed = 0;
    int failed = 0;
    try {
        Database database;
        int employeeID = database.addUser("employee", "employee-old", 2);
        int inactiveEmployeeID = database.addUser("inactive-employee", "inactive-pass", 2, false);
        int customerID = database.addUser("customer", "customer-pass", 1);
        int customerAccount = database.addAccount(customerID, 100000);
        int otherEmployeeID = database.addUser("other-employee", "other-pass", 2);
        int otherCustomerID = database.addUser("other-customer", "other-customer-pass", 1);
        int otherCustomerAccount = database.addAccount(otherCustomerID, 20000);
      database.exec("INSERT INTO transactions (account_id, amount_cents, type, balance_after_cents) VALUES (" +
                  std::to_string(customerAccount) + ", 1000, 2, 101000);");
        int managerID = database.addUser("manager", "manager-pass", 3);
        database.addAccount(managerID, 50000);

        employeePortal employee(database.handle, employeeID);
        employeeRes result = employee.changeCustomerUsername("customer", "customer-renamed");
        check(result.status && database.scalarInt64("SELECT COUNT(*) FROM users WHERE username = 'customer-renamed'") == 1,
              "employee changes customer username", passed, failed);
        check(!employee.changeCustomerUsername("manager", "manager-renamed").status,
              "employee cannot change manager username", passed, failed);
        check(!employee.changeCustomerUsername("customer-renamed", "other-customer").status,
              "duplicate username is rejected", passed, failed);

        check(employee.setCustomerActive("customer-renamed", false).status,
              "employee deactivates customer", passed, failed);
        check(database.scalarInt64("SELECT is_active FROM users WHERE user_id = ?", customerID) == 0,
              "customer active state updated", passed, failed);
        check(!employee.setCustomerActive("employee", false).status,
              "employee cannot deactivate employee", passed, failed);
        employeePortal inactiveEmployee(database.handle, inactiveEmployeeID);
        check(!inactiveEmployee.setCustomerActive("customer-renamed", true).status,
              "inactive employee cannot modify customers", passed, failed);

        int assignedLoan = database.addLoan(customerID, customerAccount, 30000, "2", employeeID);
        int unassignedLoan = database.addLoan(otherCustomerID, otherCustomerAccount, 40000, "2", otherEmployeeID);
        result = employee.getAssignedLoanApplications();
        check(result.status && result.msg.find("Loan ID: " + std::to_string(assignedLoan)) != std::string::npos,
              "employee views assigned applications", passed, failed);
      check(result.msg.find("Loan ID: " + std::to_string(unassignedLoan)) == std::string::npos,
              "employee does not see another employee's assignment", passed, failed);

        check(employee.processLoanApplication(assignedLoan, 3).status,
              "employee approves assigned loan", passed, failed);
      check(database.scalarInt64("SELECT COUNT(*) FROM loans WHERE loan_id = ? AND status = '3' AND decision_by = ?", assignedLoan, employeeID) == 1,
              "decision updates loan state", passed, failed);
        check(!employee.processLoanApplication(assignedLoan, 4).status,
              "employee cannot decide an already processed loan", passed, failed);
        check(!employee.processLoanApplication(unassignedLoan, 3).status,
              "employee cannot process another employee's loan", passed, failed);

        result = employee.getCustomerTransactionHistory("customer-renamed");
        check(result.status && result.msg.find(std::to_string(customerAccount)) != std::string::npos,
              "employee views customer transaction history", passed, failed);
        result = employee.getCustomerTransactionHistory("manager");
        check(result.status && result.msg == "No transactions found for this customer",
              "employee transaction history is limited to customers", passed, failed);

        result = employee.getAllUsersAndAccounts();
        check(result.status && result.msg.find("other-customer") != std::string::npos,
              "employee views users and accounts", passed, failed);
        check(result.msg.find("manager") == std::string::npos,
              "employee listing excludes managers", passed, failed);

        check(employee.changePassword("employee-old", "employee-new").status,
              "employee changes password", passed, failed);
        check(verifyPassword(database.textForUser("SELECT password_hash FROM users WHERE user_id = ?", employeeID), "employee-new"),
              "new employee password hash verifies", passed, failed);
        (void)inactiveEmployeeID;
    } catch (const std::exception& error) {
        std::cerr << "Test setup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return finish(passed, failed);
}

#include "test_support.hpp"
#include "../src/server/includes/administrator_portal.hpp"

using namespace testSupport;

int main() {
    int passed = 0;
    int failed = 0;
    try {
        Database database;
        int administratorID = database.addUser("admin", "admin-old", 4);
      database.addUser("other-admin", "other-admin-pass", 4);
      int inactiveAdminID = database.addUser("inactive-admin", "inactive-pass", 4, false);
        administratorPortal administrator(database.handle, administratorID);

        administratorRes result = administrator.createUser("new-customer", "customer-pass", 1);
        check(result.status, "administrator creates customer", passed, failed);
        int newCustomerID = static_cast<int>(database.scalarInt64("SELECT user_id FROM users WHERE username = 'new-customer'"));
        check(database.scalarInt64("SELECT COUNT(*) FROM accounts WHERE customer_id = ?", newCustomerID) == 1,
              "new customer receives initial account", passed, failed);
        check(database.scalarInt64("SELECT balance_cents FROM accounts WHERE customer_id = ?", newCustomerID) == 0,
              "initial customer account has zero balance", passed, failed);

        check(administrator.createUser("new-employee", "employee-pass", 2).status,
              "administrator creates employee", passed, failed);
        check(administrator.createUser("new-manager", "manager-pass", 3).status,
              "administrator creates manager", passed, failed);
        check(!administrator.createUser("new-admin", "admin-pass", 4).status,
              "administrator cannot create another administrator", passed, failed);
        check(!administrator.createUser("new-customer", "duplicate-pass", 1).status,
              "duplicate username creation rejected", passed, failed);

        int employeeID = static_cast<int>(database.scalarInt64("SELECT user_id FROM users WHERE username = 'new-employee'"));
        check(administrator.modifyUser("new-employee", "renamed-employee", 2, false).status,
              "administrator modifies employee username and active state", passed, failed);
        check(database.scalarInt64("SELECT is_active FROM users WHERE user_id = ?", employeeID) == 0,
              "administrator active-state change persisted", passed, failed);
        check(administrator.modifyUser("renamed-employee", "renamed-customer", 1, true).status,
              "administrator changes employee role to customer", passed, failed);
        check(database.scalarInt64("SELECT COUNT(*) FROM accounts WHERE customer_id = ?", employeeID) == 1,
              "role change to customer provisions missing account", passed, failed);

        check(!administrator.modifyUser("admin", "disabled-admin", 4, false).status,
              "administrator cannot modify own account", passed, failed);
        check(administrator.changePassword("admin-old", "admin-new").status,
              "administrator changes password", passed, failed);
        check(verifyPassword(database.textForUser("SELECT password_hash FROM users WHERE user_id = ?", administratorID), "admin-new"),
              "new administrator password hash verifies", passed, failed);
        check(!administrator.changePassword("admin-old", "third-pass").status,
              "administrator password change rejects wrong current password", passed, failed);

        administratorPortal inactiveAdministrator(database.handle, inactiveAdminID);
        check(!inactiveAdministrator.createUser("should-not-exist", "pass", 2).status,
              "inactive administrator cannot create users", passed, failed);
    } catch (const std::exception& error) {
        std::cerr << "Test setup/runtime error: " << error.what() << '\n';
        return 1;
    }
    return finish(passed, failed);
}

# Banking Management System

A terminal-based banking application with a TCP client/server, SQLite persistence, and four roles: customer, employee, manager, and administrator. The server owns the database and serves clients using one child process per connection.

## Requirements

- Linux with `g++`
- SQLite development library (`libsqlite3`)
- libsodium development library (`libsodium`)
- SQLite command-line utility for initializing the database

On Debian/Ubuntu, install dependencies with:

```bash
sudo apt update
sudo apt install g++ libsqlite3-dev libsodium-dev sqlite3
```

## Database Setup

Run commands from the project root. The server expects the database at `src/db/main.db` and does not create the schema automatically.

The schema script drops and recreates the banking tables. Back up any database data you need before running it:

```bash
cp src/db/main.db src/db/main.db.backup
sqlite3 src/db/main.db < src/db/create_db_schema.sql
```

Seed development users and sample data:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
  -Isrc/server/includes -Isrc/server \
  tests/seed_user_utility.cpp src/server/password_utils.cpp \
  -lsqlite3 -lsodium -o seed_user_utility
./seed_user_utility
```

The seed utility creates or updates three customers, two employees, two managers, and one administrator. All seeded users use password `pass`:

| Role | Seeded usernames |
|---|---|
| Customer | `customer1`, `customer2`, `customer3` |
| Employee | `employee1`, `employee2` |
| Manager | `manager1`, `manager2` |
| Administrator | `admin` |

The utility adds customer accounts, transaction examples, and loan examples. It is intended for development/testing; do not use the seeded credentials in a real deployment.

## Build

Build the server with all portal implementation files:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
  -Isrc/server/includes -Isrc/server \
  src/server/server.cpp \
  src/server/auth_service.cpp \
  src/server/password_utils.cpp \
  src/server/customer_portal.cpp \
  src/server/employee_portal.cpp \
  src/server/manager_portal.cpp \
  src/server/administrator_portal.cpp \
  -lsqlite3 -lsodium -o bank_server
```

Build the client:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
  src/client/client.cpp -o bank_client
```

## Run

Start the server from the project root in one terminal:

```bash
./bank_server
```

Start the client from the project root in another terminal:

```bash
./bank_client
```

The server listens on `127.0.0.1:8080`. Database and lock-file paths are relative to the project root, so run both programs from there.

## Available Operations

- **Customer:** select an owned account; view balance and transaction history; deposit, withdraw, transfer by recipient username, apply for a loan, submit feedback, change password, create another account, and log out.
- **Employee:** change a customer username or active state; view assigned loan applications and customer transaction history; approve or reject an assigned loan; view users and accounts; change password; and log out.
- **Manager:** change a customer username or active state; view loans and users/accounts; assign pending loans to active employees; review customer feedback; change password; and log out.
- **Administrator:** create customer, employee, or manager users; modify a user’s username, role, and active state; change password; and log out.

Employee and manager operations are role-restricted on the server. A customer created through administrator tools receives an initial zero-balance account. Customer signup also creates an initial account.


## Notes

- Passwords are hashed with libsodium's password-hashing API.
- Financial values are stored as integer smallest currency units, not floating point.
- The server uses SQLite transactions for financial operations and `flock`-based per-user session locks to prevent duplicate active sessions.
- This is a coursework project and has not been assessed as production banking software.

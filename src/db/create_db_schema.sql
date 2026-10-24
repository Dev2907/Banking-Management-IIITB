-- Drop existing tables if they exist
DROP TABLE IF EXISTS feedback;
DROP TABLE IF EXISTS loans;
DROP TABLE IF EXISTS transactions;
DROP TABLE IF EXISTS accounts;
DROP TABLE IF EXISTS users;

-- Create users table
CREATE TABLE users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash NOT NULL,
    role INTEGER NOT NULL CHECK (role IN (1,2,3,4)),
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0,1)),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Create accounts table
CREATE TABLE accounts (
    account_id INTEGER PRIMARY KEY AUTOINCREMENT,
    customer_id INTEGER NOT NULL REFERENCES users(user_id),
    balance_cents INTEGER NOT NULL CHECK (balance_cents >= 0),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Create transactions table
CREATE TABLE transactions (
    transaction_id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL REFERENCES accounts(account_id),
    amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
    type INTEGER NOT NULL CHECK (type IN (1,2)),
    reference_id INTEGER REFERENCES transactions(transaction_id),
    timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    balance_after_cents INTEGER NOT NULL CHECK (balance_after_cents > 0)
);

-- Create loans table
CREATE TABLE loans (
    loan_id INTEGER PRIMARY KEY AUTOINCREMENT,
    customer_id INTEGER NOT NULL REFERENCES users(user_id),
    account_id INTEGER NOT NULL REFERENCES accounts(account_id),
    amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
    status TEXT NOT NULL CHECK (status IN ('1','2','3','4','5')),
    assigned_employee_id INTEGER REFERENCES users(user_id),
    decision_by INTEGER REFERENCES users(user_id),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    decision_at TEXT
);

-- Create feedback table
CREATE TABLE feedback (
    feedback_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(user_id),
    message TEXT NOT NULL,
    timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
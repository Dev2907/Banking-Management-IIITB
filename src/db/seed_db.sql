-- Enable foreign key constraints
PRAGMA foreign_keys = ON;

-- Insert dummy users
INSERT INTO users (user_id, username, password_hash, role, is_active, created_at)
VALUES 
(1, 'alice', 'hash123', 1, 1, '2023-10-01 10:00:00'),
(2, 'bob', 'hash456', 2, 1, '2023-10-01 10:05:00'),
(3, 'charlie', 'hash789', 3, 1, '2023-10-01 10:10:00');

-- Insert dummy accounts
INSERT INTO accounts (account_id, customer_id, balance_cents, created_at)
VALUES 
(1, 1, 50000, '2023-10-01 10:15:00'),
(2, 2, 100000, '2023-10-01 10:20:00');

-- Insert dummy loans
INSERT INTO loans (loan_id, customer_id, account_id, amount_cents, status, assigned_employee_id, decision_by, created_at, decision_at)
VALUES 
(1, 1, 1, 20000, 1, 2, 2, '2023-10-01 10:25:00', '2023-10-01 10:30:00'),
(2, 2, 2, 30000, 2, 3, 3, '2023-10-01 10:35:00', '2023-10-01 10:40:00');

-- Insert dummy transactions
INSERT INTO transactions (transaction_id, account_id, amount_cents, type, reference_id, timestamp, balance_after_cents)
VALUES 
(1, 1, 5000, 1, NULL, '2023-10-01 10:45:00', 55000),
(2, 2, 10000, 2, 1, '2023-10-01 10:50:00', 90000);

-- Insert dummy feedback
INSERT INTO feedback (feedback_id, user_id, message, timestamp)
VALUES 
(1, 1, 'Great service!', '2023-10-01 11:00:00'),
(2, 2, 'Needs improvement.', '2023-10-01 11:05:00');
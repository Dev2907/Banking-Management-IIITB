#include <iostream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

const int PORT = 8080;
const char* ADD = "127.0.0.1";

using namespace std;

/*
1 = Customer
2 = Employee
3 = Manager
4 = Administrator
*/

void destroyClient(int clientFD){
    close(clientFD);
}

bool recvLine(
    int clientFD,
    string& line,
    size_t maxLength = 1024
) {
    line.clear();

    while (true) {
        char ch;

        ssize_t n = recv(clientFD, &ch, 1, 0);

        if (n == 0) {
            // Client disconnected.
            return false;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("recv error");
            return false;
        }

        if (ch == '\n') {
            return true;
        }

        if (line.size() >= maxLength) {
            cerr << "Request field too long\n";
            return false;
        }

        line.push_back(ch);
    }
}

bool sendAll(int fd, const string& data) {
    size_t totalSent = 0;

    while (totalSent < data.size()) {
        ssize_t n = send(
            fd,
            data.data() + totalSent,
            data.size() - totalSent,
            MSG_NOSIGNAL
        );

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            perror("[server] send error");
            return false;
        }

        totalSent += static_cast<size_t>(n);
    }

    return true;
}

bool sendLine(int fd, const string& line) {
    return sendAll(fd, line + "\n");
}

bool readCountedLines(int fd, const string& errorContext) {
    string status;
    string lineCountText;
    if (!recvLine(fd, status, 1) || !recvLine(fd, lineCountText, 16)) {
        cerr << "Invalid " << errorContext << " response" << endl;
        return false;
    }

    try {
        size_t lineCount = stoul(lineCountText);
        if (lineCount == 0) {
            string message;
            if (!recvLine(fd, message, 4096)) {
                cerr << "Invalid " << errorContext << " response" << endl;
                return false;
            }
            cout << message << endl;
            return true;
        }

        for (size_t index = 0; index < lineCount; ++index) {
            string message;
            if (!recvLine(fd, message, 4096)) {
                cerr << "Invalid " << errorContext << " response" << endl;
                return false;
            }
            cout << message << endl;
        }
    } catch (const exception&) {
        cerr << "Invalid " << errorContext << " list response" << endl;
        return false;
    }

    return true;
}

void customerLogin(int fd){
    string accountCountLine;
    if (!recvLine(fd, accountCountLine, 32)) {
        cerr << "Unable to receive customer accounts" << endl;
        return;
    }

    int accountCount;
    try {
        accountCount = stoi(accountCountLine);
    } catch (const exception&) {
        cerr << "Invalid account list response" << endl;
        return;
    }

    if (accountCount == 0) {
        cerr << "No accounts are associated with this user" << endl;
        return;
    }

    vector<pair<int, string>> accounts;
    for (int index = 0; index < accountCount; ++index) {
        string accountID;
        string balance;
        if (!recvLine(fd, accountID, 32) || !recvLine(fd, balance, 32)) {
            cerr << "Invalid account list response" << endl;
            return;
        }
        try {
            accounts.emplace_back(stoi(accountID), balance);
        } catch (const exception&) {
            cerr << "Invalid account list response" << endl;
            return;
        }
    }

    if (accountCount > 1) {
        cout << "Select an account for this session:\n";
        for (const auto& account : accounts) {
            cout << "Account " << account.first
                 << " | Balance: " << account.second << "\n";
        }

        int selectedAccount;
        cout << "Account ID: ";
        cin >> selectedAccount;
        sendLine(fd, to_string(selectedAccount));

        string selectionStatus;
        string selectionMessage;
        if (!recvLine(fd, selectionStatus, 1) ||
            !recvLine(fd, selectionMessage)) {
            cerr << "Invalid account selection response" << endl;
            return;
        }
        if (selectionStatus != "1") {
            cerr << selectionMessage << endl;
            return;
        }
    }

    while(true){
        cout << "--- Customer Menu ---\n";
        cout << "1. View Account Balance\n";
        cout << "2. Deposit Money\n";
        cout << "3. Withdraw Money\n";
        cout << "4. Transfer Funds\n";
        cout << "5. Apply for a Loan\n";
        cout << "6. View Transaction History\n";
        cout << "7. Change Password\n";
        cout << "8. Submit Feedback\n";
        cout << "9. Create New Account\n";
        cout << "10. Logout\n";

        cout << "Enter choice: ";
        int choice;
        cin >> choice;

        string status;
        string msg; 
        string amount;
        string input;
        bool transactionHistory = false;
        switch (choice){
            case 1:
                sendLine(fd, "GET_BALANCE");
                break;
            case 2:
                sendLine(fd, "DEPOSIT");
                cout << "Amount to be deposited: ";
                cin >> amount;
                sendLine(fd, amount);
                break;
            case 3:
                sendLine(fd, "WITHDRAW");
                cout << "Amount to be Withdrawn: ";
                cin >> amount;
                sendLine(fd, amount);
                break;
            case 4: {
                string recipientUsername;
                cout << "Recipient username: ";
                cin >> recipientUsername;
                cout << "Amount to transfer: ";
                cin >> amount;
                sendLine(fd, "TRANSFER");
                sendLine(fd, recipientUsername);
                sendLine(fd, amount);
                break;
            }
            case 5:
                cout << "Loan amount: ";
                cin >> amount;
                sendLine(fd, "APPLY_LOAN");
                sendLine(fd, amount);
                break;
            case 6:
                sendLine(fd, "TRANSACTION_HISTORY");
                transactionHistory = true;
                break;
            case 7: {
                string currentPassword;
                string newPassword;
                cout << "Current password: ";
                cin >> currentPassword;
                cout << "New password: ";
                cin >> newPassword;
                sendLine(fd, "CHANGE_PASSWORD");
                sendLine(fd, currentPassword);
                sendLine(fd, newPassword);
                break;
            }
            case 8:
                cout << "Feedback: ";
                getline(cin >> ws, input);
                sendLine(fd, "FEEDBACK");
                sendLine(fd, input);
                break;
            case 9:
                sendLine(fd, "ADD_ACCOUNT");
                break;
            case 10:
                sendLine(fd, "LOGOUT");
                return;
            default:
                continue;
        }
        if (transactionHistory) {
            string lineCountText;
            if (!recvLine(fd, status) || !recvLine(fd, lineCountText, 16)) {
                cerr << "Invalid Response" << endl;
                continue;
            }

            try {
                size_t lineCount = stoul(lineCountText);
                if (lineCount == 0) {
                    if (!recvLine(fd, msg, 4096)) {
                        cerr << "Invalid Response" << endl;
                        continue;
                    }
                    cout << msg << endl;
                } else {
                    for (size_t index = 0; index < lineCount; ++index) {
                        if (!recvLine(fd, msg, 4096)) {
                            cerr << "Invalid Response" << endl;
                            break;
                        }
                        cout << msg << endl;
                    }
                }
            } catch (const exception&) {
                cerr << "Invalid transaction history response" << endl;
            }
        }else if(!recvLine(fd, status) || !recvLine(fd, msg, 4096)){
            cerr << "Invalid Response" << endl;    
        }else{
            cout << msg << endl;
        }
    }
}

void employeeLogin(int fd) {
    while (true) {
        cout << "--- Employee Menu ---\n";
        cout << "1. Change Customer Username\n";
        cout << "2. Activate/Deactivate Customer\n";
        cout << "3. View Assigned Loan Applications\n";
        cout << "4. View Customer Transaction History\n";
        cout << "5. Change Password\n";
        cout << "6. Process Assigned Loan\n";
        cout << "7. View All Users and Accounts\n";
        cout << "8. Logout\n";
        cout << "Enter choice: ";

        int choice;
        if (!(cin >> choice)) {
            return;
        }

        if (choice == 1) {
            string currentUsername;
            string newUsername;
            cout << "Current customer username: ";
            cin >> currentUsername;
            cout << "New username: ";
            cin >> newUsername;
            sendLine(fd, "EMP_CHANGE_USERNAME");
            sendLine(fd, currentUsername);
            sendLine(fd, newUsername);
        } else if (choice == 2) {
            string customerUsername;
            int isActive;
            cout << "Customer username: ";
            cin >> customerUsername;
            cout << "Set active (1) or inactive (0): ";
            cin >> isActive;
            sendLine(fd, "EMP_SET_ACTIVE");
            sendLine(fd, customerUsername);
            sendLine(fd, to_string(isActive));
        } else if (choice == 3) {
            sendLine(fd, "EMP_ASSIGNED_LOANS");
        } else if (choice == 4) {
            string customerUsername;
            cout << "Customer username: ";
            cin >> customerUsername;
            sendLine(fd, "EMP_CUSTOMER_HISTORY");
            sendLine(fd, customerUsername);
        } else if (choice == 5) {
            string currentPassword;
            string newPassword;
            cout << "Current password: ";
            cin >> currentPassword;
            cout << "New password: ";
            cin >> newPassword;
            sendLine(fd, "EMP_CHANGE_PASSWORD");
            sendLine(fd, currentPassword);
            sendLine(fd, newPassword);
        } else if (choice == 6) {
            string loanID;
            string decision;
            cout << "Loan ID: ";
            cin >> loanID;
            cout << "Decision (3 = approve, 4 = reject): ";
            cin >> decision;
            sendLine(fd, "EMP_PROCESS_LOAN");
            sendLine(fd, loanID);
            sendLine(fd, decision);
        } else if (choice == 7) {
            sendLine(fd, "EMP_VIEW_USERS");
        } else if (choice == 8) {
            sendLine(fd, "LOGOUT");
            return;
        } else {
            continue;
        }

        string status;
        string message;
        if (choice == 3 || choice == 4 || choice == 7) {
            if (!readCountedLines(fd, "employee")) {
                return;
            }
            continue;
        }

        if (!recvLine(fd, status, 1) || !recvLine(fd, message)) {
            cerr << "Invalid employee response" << endl;
            return;
        }
        cout << message << endl;
    }
}

void managerLogin(int fd) {
    while (true) {
        cout << "--- Manager Menu ---\n";
        cout << "1. Change Customer Username\n";
        cout << "2. Activate/Deactivate User\n";
        cout << "3. View Loan Applications\n";
        cout << "4. View All Users and Accounts\n";
        cout << "5. Assign Loan Application\n";
        cout << "6. Review Customer Feedback\n";
        cout << "7. Change Password\n";
        cout << "8. Logout\n";
        cout << "Enter choice: ";

        int choice;
        if (!(cin >> choice)) {
            return;
        }

        if (choice == 1) {
            string currentUsername;
            string newUsername;
            cout << "Current customer username: ";
            cin >> currentUsername;
            cout << "New username: ";
            cin >> newUsername;
            sendLine(fd, "MGR_CHANGE_CUSTOMER_USERNAME");
            sendLine(fd, currentUsername);
            sendLine(fd, newUsername);
        } else if (choice == 2) {
            string username;
            int isActive;
            cout << "Customer username: ";
            cin >> username;
            cout << "Set active (1) or inactive (0): ";
            cin >> isActive;
            sendLine(fd, "MGR_SET_USER_ACTIVE");
            sendLine(fd, username);
            sendLine(fd, to_string(isActive));
        } else if (choice == 3) {
            sendLine(fd, "MGR_VIEW_LOANS");
            if (!readCountedLines(fd, "manager")) {
                return;
            }
            continue;
        } else if (choice == 4) {
            sendLine(fd, "MGR_VIEW_USERS");
            if (!readCountedLines(fd, "manager")) {
                return;
            }
            continue;
        } else if (choice == 5) {
            string employeeUsername;
            string loanID;
            cout << "Employee username: ";
            cin >> employeeUsername;
            cout << "Loan ID: ";
            cin >> loanID;
            sendLine(fd, "MGR_ASSIGN_LOAN");
            sendLine(fd, employeeUsername);
            sendLine(fd, loanID);
        } else if (choice == 6) {
            sendLine(fd, "MGR_VIEW_FEEDBACK");
            if (!readCountedLines(fd, "manager")) {
                return;
            }
            continue;
        } else if (choice == 7) {
            string currentPassword;
            string newPassword;
            cout << "Current password: ";
            cin >> currentPassword;
            cout << "New password: ";
            cin >> newPassword;
            sendLine(fd, "MGR_CHANGE_PASSWORD");
            sendLine(fd, currentPassword);
            sendLine(fd, newPassword);
        } else if (choice == 8) {
            sendLine(fd, "LOGOUT");
            return;
        } else {
            continue;
        }

        string status;
        string message;
        if (!recvLine(fd, status, 1) || !recvLine(fd, message)) {
            cerr << "Invalid manager response" << endl;
            return;
        }
        cout << message << endl;
    }
}

void administratorLogin(int fd) {
    while (true) {
        cout << "--- Administrator Menu ---\n";
        cout << "1. Create User (roles 1-3)\n";
        cout << "2. Modify User\n";
        cout << "3. Change Password\n";
        cout << "4. Logout\n";
        cout << "Enter choice: ";

        int choice;
        if (!(cin >> choice)) {
            return;
        }

        if (choice == 1) {
            string username;
            string password;
            int role;
            cout << "New username: ";
            cin >> username;
            cout << "Password: ";
            cin >> password;
            cout << "Role (1 = customer, 2 = employee, 3 = manager): ";
            cin >> role;
            sendLine(fd, "ADMIN_CREATE_USER");
            sendLine(fd, username);
            sendLine(fd, password);
            sendLine(fd, to_string(role));
        } else if (choice == 2) {
            string currentUsername;
            string newUsername;
            int role;
            int isActive;
            cout << "Current username: ";
            cin >> currentUsername;
            cout << "New username: ";
            cin >> newUsername;
            cout << "Role (1 = customer, 2 = employee, 3 = manager, 4 = administrator): ";
            cin >> role;
            cout << "Set active (1) or inactive (0): ";
            cin >> isActive;
            sendLine(fd, "ADMIN_MODIFY_USER");
            sendLine(fd, currentUsername);
            sendLine(fd, newUsername);
            sendLine(fd, to_string(role));
            sendLine(fd, to_string(isActive));
        } else if (choice == 3) {
            string currentPassword;
            string newPassword;
            cout << "Current password: ";
            cin >> currentPassword;
            cout << "New password: ";
            cin >> newPassword;
            sendLine(fd, "ADMIN_CHANGE_PASSWORD");
            sendLine(fd, currentPassword);
            sendLine(fd, newPassword);
        } else if (choice == 4) {
            sendLine(fd, "LOGOUT");
            return;
        } else {
            continue;
        }

        string status;
        string message;
        if (!recvLine(fd, status, 1) || !recvLine(fd, message)) {
            cerr << "Invalid administrator response" << endl;
            return;
        }
        cout << message << endl;
    }
}

int main(){
    int clientFD = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFD < 0){
        perror("socket error");
        return 1;
    }

    sockaddr_in serverAd{};
    serverAd.sin_family = AF_INET;
    serverAd.sin_port = htons(PORT);

    if(inet_pton(AF_INET, ADD, &serverAd.sin_addr) != 1){
        perror("iner_pton");
        return 1;
    }

    if(connect(clientFD, (sockaddr *)&serverAd, sizeof(serverAd)) < 0){
        perror("Connect error");
        return 1;
    }

    cout << "--- Connected to server --- " << endl;
    cout << "Server port: " << PORT << endl;
    cout << "Server add: " << ADD << endl;

    while(true){
        cout << "--- Banking Management System ---" << endl;
        cout << "Select Role to login:\n1. Customer\n2. Bank Employee\n3. Manager\n4. Administrator\n5. Exit" << endl;
        int role;
        cout << "Enter choice: ";
        cin >> role;
        string operation = "login";
        switch (role){
            case 1:
                cout << "--- Customer Access ---" << endl;
                cout << "1. Existing Account Login\n2. Create New account\n";
                cout << "Enter choice: ";
                int choice;
                cin >> choice;
                if (choice == 1){
                    operation = "login";
                }else{
                    operation = "signup";
                }
                break;
            case 2:
                cout << "--- Employee Access ---" << endl;
                break;
            case 3:
                cout << "--- Manager Access ---" << endl;
                break;
            case 4:
                cout << "--- Administrator Login ---" << endl;
                break;
            default:
                cout << "Good Bye!" << endl;
                destroyClient(clientFD);
                return 0;
        }
        
        cin.ignore(); // Discards the single '\n' left behind
        string username;
        string password;
        cout << "Enter Username: ";
        cin >> username;
        cout << "Enter Password: ";
        cin >> password;
        cout << endl;
        
        if (username.empty() || password.empty()) {
            cerr << "Username and password cannot be empty\n";
            continue;
        }
        
        if (username.find_first_of("\r\n") != string::npos ||
        password.find_first_of("\r\n") != string::npos) {
            cerr << "Username or password contains an invalid character\n";
            continue;
        }
        
        
        if (!sendLine(clientFD, operation) ||
            !sendLine(clientFD, to_string(role)) ||
            !sendLine(clientFD, username) ||
            !sendLine(clientFD, password)) {
            cerr << "Failed to send request" << endl;
            destroyClient(clientFD);
            return 1;
        }

        string status;
        string message;
        string userID;
        string role_rec;
        if(!recvLine(clientFD, status, 1) || 
            !recvLine(clientFD, message) || 
            !recvLine(clientFD, userID) || 
            !recvLine(clientFD, role_rec, 1)
        ){
            cerr << "Invalid Response" << endl;
            destroyClient(clientFD);
            return 1;      
        }
        if(status == "0"){
            cout << message << endl;
            destroyClient(clientFD);
            return 1;
        }
        if(stoi(role_rec) == 1){
            customerLogin(clientFD);
            destroyClient(clientFD);
            return 0;
        }
        if(stoi(role_rec) == 2){
            employeeLogin(clientFD);
            destroyClient(clientFD);
            return 0;
        }
        if(stoi(role_rec) == 3){
            managerLogin(clientFD);
            destroyClient(clientFD);
            return 0;
        }
        if(stoi(role_rec) == 4){
            administratorLogin(clientFD);
            destroyClient(clientFD);
            return 0;
        }
    }
    

}
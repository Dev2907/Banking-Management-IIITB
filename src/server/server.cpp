#include "includes/auth_service.hpp"
#include "includes/customer_portal.hpp"
#include "includes/employee_portal.hpp"
#include "includes/manager_portal.hpp"
#include "includes/administrator_portal.hpp"

#include <algorithm>
#include <iostream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sodium.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <cerrno>

const int PORT = 8080;
const string DBPATH = "src/db/main.db";
const string LOCKPATH = "src/locks/session_user_";

using namespace std;

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

bool sendLines(int fd, const string& lines) {
    if (lines.empty()) {
        return sendLine(fd, "");
    }

    size_t start = 0;
    while (start < lines.size()) {
        size_t end = lines.find('\n', start);
        if (end == string::npos) {
            return sendLine(fd, lines.substr(start));
        }
        if (!sendLine(fd, lines.substr(start, end - start))) {
            return false;
        }
        start = end + 1;
    }

    return true;
}

class sessionLock{
    private:
        int fd_ = -1;
    public:
        sessionLock() = default; //default constructor
        sessionLock(const sessionLock&) = delete; //disallow copying construction
        sessionLock& operator = (const sessionLock&) = delete; //disallow copy assignment

    bool acquire(int userID){
        const string path = LOCKPATH + to_string(userID) + ".lock";

        fd_ = open(path.c_str(), O_RDWR|O_CREAT, 0600);

        if(fd_ == -1){
            perror("Failed to open session lock file");
            return false;
        }

        if(flock(fd_, LOCK_EX|LOCK_NB) == -1){
            int savedErr = errno;
            close(fd_);
            fd_ = -1;

            if(savedErr == EWOULDBLOCK || savedErr == EAGAIN){
                cerr << "[Server] User has already acquired one session" << endl;
            }else{
                cerr << strerror(savedErr) << endl;
            }
            return false;
        }

        cout << "Session Lock acquired user:" << userID << endl;

        return true;
    }
    ~sessionLock(){
        if (fd_ != -1){
            flock(fd_, LOCK_UN);
            close(fd_);
            cout << "Session lock released" << endl;
        }
    }
};

void customerSession(sqlite3* db ,int clientFD, const int customerID, const string username){  
    customerPortal customer(db, customerID);
    auto sendCustomerResult = [&](const custRes& res) {
        return sendLine(clientFD, to_string(res.status)) &&
            sendLines(clientFD, res.msg);
    };

    vector<customerAccount> accounts;
    custRes accountResult;
    if (!customer.listAccounts(accounts, accountResult)) {
        sendLine(clientFD, "0");
        sendLines(clientFD, accountResult.msg);
        return;
    }
    if (!sendLine(clientFD, to_string(accounts.size()))) {
        return;
    }
    for (const customerAccount& account : accounts) {
        if (!sendLine(clientFD, to_string(account.accountID)) ||
            !sendLine(clientFD, to_string(account.balance))) {
            return;
        }
    }

    if (accounts.size() == 1) {
        accountResult = customer.selectAccount(accounts.front().accountID);
    } else if (!accounts.empty()) {
        string selectedAccount;
        if (!recvLine(clientFD, selectedAccount, 32)) {
            return;
        }

        try {
            accountResult = customer.selectAccount(stoi(selectedAccount));
        } catch (const exception&) {
            accountResult.msg = "Invalid account selection";
        }

        if (!sendCustomerResult(accountResult) || !accountResult.status) {
            return;
        }
    }

    while(true){
        string operation;
        if(!recvLine(clientFD, operation)){
            cout << "Customer disconnected: "
                << username << '\n';
            break;
        }
        if(operation == "GET_BALANCE"){
            custRes res = customer.getBalance();
            if (res.status) res.msg = "Current Balance: " + res.msg;
            sendCustomerResult(res);
        }else if(operation == "ADD_ACCOUNT"){
            sendCustomerResult(customer.createAccount());
        }else if(operation == "DEPOSIT"){
            string amount;
            if(!recvLine(clientFD, amount)){
                cout << "Customer disconnected: "
                << username << '\n';
                break;
            }
            custRes res;
            try {
                res = customer.deposit(stoll(amount));
            } catch (const exception&) {
                res.msg = "Invalid deposit amount";
            }
            if (res.status) res.msg = "Updated Balance: " + res.msg;
            sendCustomerResult(res);
        }else if(operation == "WITHDRAW"){
            string amount;
            if(!recvLine(clientFD, amount)){
                cout << "Customer disconnected: "
                << username << '\n';
                break;
            }
            custRes res;
            try {
                res = customer.withdraw(stoll(amount));
            } catch (const exception&) {
                res.msg = "Invalid withdrawal amount";
            }
            if (res.status) res.msg = "Updated Balance: " + res.msg;
            sendCustomerResult(res);
        }else if(operation == "TRANSFER"){
            string recipientUsername;
            string amount;
            if (!recvLine(clientFD, recipientUsername, 64) || !recvLine(clientFD, amount, 32)) {
                break;
            }
            custRes res;
            try {
                res = customer.transfer(recipientUsername, stoll(amount));
            } catch (const exception&) {
                res.msg = "Invalid transfer amount";
            }
            if (res.status) res.msg = "Transfer complete. Updated Balance: " + res.msg;
            sendCustomerResult(res);
        }else if(operation == "APPLY_LOAN"){
            string amount;
            if (!recvLine(clientFD, amount, 32)) {
                break;
            }
            custRes res;
            try {
                res = customer.applyForLoan(stoll(amount));
            } catch (const exception&) {
                res.msg = "Invalid loan amount";
            }
            sendCustomerResult(res);
        }else if(operation == "TRANSACTION_HISTORY"){
            custRes res = customer.getTransactionHistory();
            size_t lineCount = 0;
            if (res.status && res.msg != "No transactions for this account") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
        }else if(operation == "CHANGE_PASSWORD"){
            string currentPassword;
            string newPassword;
            if (!recvLine(clientFD, currentPassword, 1024) ||
                !recvLine(clientFD, newPassword, 1024)) {
                break;
            }
            sendCustomerResult(customer.changePassword(currentPassword, newPassword));
        }else if(operation == "FEEDBACK"){
            string message;
            if (!recvLine(clientFD, message, 2000)) {
                break;
            }
            sendCustomerResult(customer.submitFeedback(message));
        }
        
        else if (operation == "LOGOUT") {
            sendLine(clientFD, "LOGOUT_SUCCESS");
            cout << "Customer logged out: "
                 << username << '\n';
            break;
        }
    }  
}

void employeeSession(sqlite3* db, int clientFD, int employeeID, const string& username) {
    employeePortal employee(db, employeeID);
    while (true) {
        string operation;
        if (!recvLine(clientFD, operation)) {
            break;
        }

        if (operation == "EMP_CHANGE_USERNAME") {
            string currentUsername;
            string newUsername;
            if (!recvLine(clientFD, currentUsername, 64) ||
                !recvLine(clientFD, newUsername, 64)) {
                break;
            }
            employeeRes res = employee.changeCustomerUsername(currentUsername, newUsername);
            sendLine(clientFD, to_string(res.status));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_SET_ACTIVE") {
            string customerUsername;
            string activeValue;
            if (!recvLine(clientFD, customerUsername, 64) ||
                !recvLine(clientFD, activeValue, 1)) {
                break;
            }
            if (activeValue != "0" && activeValue != "1") {
                sendLine(clientFD, "0");
                sendLine(clientFD, "Active state must be 0 or 1");
                continue;
            }
            employeeRes res = employee.setCustomerActive(customerUsername, activeValue == "1");
            sendLine(clientFD, to_string(res.status));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_ASSIGNED_LOANS") {
            employeeRes res = employee.getAssignedLoanApplications();
            size_t lineCount = 0;
            if (res.status && res.msg != "No loan applications are assigned to you") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_VIEW_USERS") {
            employeeRes res = employee.getAllUsersAndAccounts();
            size_t lineCount = 0;
            if (res.status && res.msg != "No users found") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_PROCESS_LOAN") {
            string loanIDText;
            string decisionText;
            if (!recvLine(clientFD, loanIDText, 32) ||
                !recvLine(clientFD, decisionText, 1)) {
                break;
            }

            employeeRes res;
            try {
                res = employee.processLoanApplication(
                    stoi(loanIDText),
                    stoi(decisionText)
                );
            } catch (const exception&) {
                res.msg = "Invalid loan ID or decision";
            }
            sendLine(clientFD, to_string(res.status));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_CUSTOMER_HISTORY") {
            string customerUsername;
            if (!recvLine(clientFD, customerUsername, 64)) {
                break;
            }
            employeeRes res = employee.getCustomerTransactionHistory(customerUsername);
            size_t lineCount = 0;
            if (res.status && res.msg != "No transactions found for this customer") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
        } else if (operation == "EMP_CHANGE_PASSWORD") {
            string currentPassword;
            string newPassword;
            if (!recvLine(clientFD, currentPassword, 1024) ||
                !recvLine(clientFD, newPassword, 1024)) {
                break;
            }
            employeeRes res = employee.changePassword(currentPassword, newPassword);
            sendLine(clientFD, to_string(res.status));
            sendLines(clientFD, res.msg);
        } else if (operation == "LOGOUT") {
            sendLine(clientFD, "LOGOUT_SUCCESS");
            cout << "Employee logged out: " << username << '\n';
            break;
        }
    }
}

void managerSession(sqlite3* db, int clientFD, int managerID, const string& username) {
    managerPortal manager(db, managerID);
    while (true) {
        string operation;
        if (!recvLine(clientFD, operation)) {
            break;
        }

        managerRes res;
        if (operation == "MGR_CHANGE_CUSTOMER_USERNAME") {
            string currentUsername;
            string newUsername;
            if (!recvLine(clientFD, currentUsername, 64) ||
                !recvLine(clientFD, newUsername, 64)) {
                break;
            }
            res = manager.changeCustomerUsername(currentUsername, newUsername);
        } else if (operation == "MGR_SET_USER_ACTIVE") {
            string targetUsername;
            string activeText;
            if (!recvLine(clientFD, targetUsername, 64) ||
                !recvLine(clientFD, activeText, 1)) {
                break;
            }
            if (activeText != "0" && activeText != "1") {
                res.msg = "Active state must be 0 or 1";
            } else {
                res = manager.setCustomerActive(targetUsername, activeText == "1");
            }
        } else if (operation == "MGR_ASSIGN_LOAN") {
            string employeeUsername;
            string loanIDText;
            if (!recvLine(clientFD, employeeUsername, 64) ||
                !recvLine(clientFD, loanIDText, 32)) {
                break;
            }
            try {
                res = manager.assignLoanApplication(employeeUsername, stoi(loanIDText));
            } catch (const exception&) {
                res.msg = "Invalid loan ID";
            }
        } else if (operation == "MGR_VIEW_LOANS") {
            res = manager.getLoanApplications();
            size_t lineCount = 0;
            if (res.status && res.msg != "No loan applications found") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
            continue;
        } else if (operation == "MGR_VIEW_USERS") {
            res = manager.getAllUsersAndAccounts();
            size_t lineCount = 0;
            if (res.status && res.msg != "No users found") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
            continue;
        } else if (operation == "MGR_VIEW_FEEDBACK") {
            res = manager.getCustomerFeedback();
            size_t lineCount = 0;
            if (res.status && res.msg != "No customer feedback found") {
                lineCount = 1 + static_cast<size_t>(count(res.msg.begin(), res.msg.end(), '\n'));
            }
            sendLine(clientFD, to_string(res.status));
            sendLine(clientFD, to_string(lineCount));
            sendLines(clientFD, res.msg);
            continue;
        } else if (operation == "MGR_CHANGE_PASSWORD") {
            string currentPassword;
            string newPassword;
            if (!recvLine(clientFD, currentPassword, 1024) ||
                !recvLine(clientFD, newPassword, 1024)) {
                break;
            }
            res = manager.changePassword(currentPassword, newPassword);
        } else if (operation == "LOGOUT") {
            sendLine(clientFD, "LOGOUT_SUCCESS");
            cout << "Manager logged out: " << username << '\n';
            break;
        } else {
            res.msg = "Unknown manager operation";
        }

        sendLine(clientFD, to_string(res.status));
        sendLines(clientFD, res.msg);
    }
}

void administratorSession(
    sqlite3* db,
    int clientFD,
    int administratorID,
    const string& username
) {
    administratorPortal administrator(db, administratorID);
    while (true) {
        string operation;
        if (!recvLine(clientFD, operation)) {
            break;
        }

        administratorRes res;
        if (operation == "ADMIN_CREATE_USER") {
            string newUsername;
            string password;
            string roleText;
            if (!recvLine(clientFD, newUsername, 64) ||
                !recvLine(clientFD, password, 1024) ||
                !recvLine(clientFD, roleText, 1)) {
                break;
            }
            try {
                res = administrator.createUser(newUsername, password, stoi(roleText));
            } catch (const exception&) {
                res.msg = "Invalid role";
            }
        } else if (operation == "ADMIN_MODIFY_USER") {
            string currentUsername;
            string newUsername;
            string roleText;
            string activeText;
            if (!recvLine(clientFD, currentUsername, 64) ||
                !recvLine(clientFD, newUsername, 64) ||
                !recvLine(clientFD, roleText, 1) ||
                !recvLine(clientFD, activeText, 1)) {
                break;
            }
            try {
                int role = stoi(roleText);
                if (activeText != "0" && activeText != "1") {
                    res.msg = "Active state must be 0 or 1";
                } else {
                    res = administrator.modifyUser(
                        currentUsername,
                        newUsername,
                        role,
                        activeText == "1"
                    );
                }
            } catch (const exception&) {
                res.msg = "Invalid role or active state";
            }
        } else if (operation == "ADMIN_CHANGE_PASSWORD") {
            string currentPassword;
            string newPassword;
            if (!recvLine(clientFD, currentPassword, 1024) ||
                !recvLine(clientFD, newPassword, 1024)) {
                break;
            }
            res = administrator.changePassword(currentPassword, newPassword);
        } else if (operation == "LOGOUT") {
            sendLine(clientFD, "LOGOUT_SUCCESS");
            cout << "Administrator logged out: " << username << '\n';
            break;
        } else {
            res.msg = "Unknown administrator operation";
        }

        sendLine(clientFD, to_string(res.status));
        sendLines(clientFD, res.msg);
    }
}

void handleClient(int clientFD){
    sqlite3* db = nullptr;
    int rc = sqlite3_open(DBPATH.c_str(), &db);

    if(rc != SQLITE_OK){
        cerr << "Couldnt connect to database: " << (db? sqlite3_errmsg16(db): "Unable to open db") << endl;

        sendLine(clientFD, "Internal server error");
        if(db != nullptr){
            sqlite3_close(db);
        }
        close(clientFD);
        _exit(1);
    }

    // This helper sends a response and closes the resources
    // when we're finished handling this request.
    auto finish = [&](const string& response) {
        sendLines(clientFD, response);

        sqlite3_close(db);
        close(clientFD);
    };

    string operation;
    string role;
    string username;
    string password;
    
    if(!recvLine(clientFD, operation, 32) || 
        !recvLine(clientFD, role, 1) ||
        !recvLine(clientFD, username, 1024) ||
        !recvLine(clientFD, password, 1024)
    ){
        cerr << "Invalid request" << endl;
        finish("Invalid Request");
        _exit(1);
    }

    if(operation == "login" || operation == "signup"){
        try{
            authService auth(db);
            authRes res;
            if (operation == "signup") {
                if (stoi(role) != static_cast<int>(userRoles::Customer)) {
                    res.message = "Only customer registration is available";
                } else {
                    res = auth.registerCustomer(username, password);
                }
            } else {
                res = auth.authenticate(username, password, stoi(role));
            }
            
            string response =
                to_string(res.success) + "\n" +
                res.message +
                to_string(res.userID) + "\n" +
                to_string(static_cast<int>(res.role)) + "\n";
            
            
            if (res.success){
                sessionLock lock;
                if(!lock.acquire(res.userID)){
                    string rejection =
                        string("0\nThere is already an existing session for this user\n") +
                        "-1\n" + role + "\n";
                    finish(rejection);
                    _exit(1);
                }
                sendLines(clientFD, response);
                if(res.role == userRoles::Customer){
                    customerSession(db, clientFD, res.userID, username);
                    _exit(0);
                }
                if(res.role == userRoles::Employee){
                    employeeSession(db, clientFD, res.userID, username);
                    _exit(0);
                }
                if(res.role == userRoles::Manager){
                    managerSession(db, clientFD, res.userID, username);
                    _exit(0);
                }
                if(res.role == userRoles::Administrator){
                    administratorSession(db, clientFD, res.userID, username);
                    _exit(0);
                }
                finish("Session completed");
            }else{
                finish(response);
                _exit(1);
            }
        }catch(const exception& e){
            cerr << "[Server] Auth Error: "<<  e.what() << endl;
            finish("Authentication failure");
            _exit(1);
        }
    }
}

int main(){

    if (sodium_init() < 0) {
        std::cerr << "Libsodium initialization failed\n";
        return 1;
    }

    int serverFD = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFD == -1){
        perror("Server socket error");
        return 1;
    }

    sockaddr_in serverAd{};
    serverAd.sin_family = AF_INET;
    serverAd.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAd.sin_port = htons(PORT);

    if(bind(serverFD, (sockaddr *)&serverAd, sizeof(serverAd)) == -1){
        perror("bind error");
        return 1;
    }

    if(listen(serverFD, 10) == -1){
        perror("listen errro");
        return 1;
    }

    cout << "--- Server active ---" << endl;
    cout << "port:" << PORT << endl;
    cout << "addr: " << INADDR_LOOPBACK << endl;
    cout << endl;

    while (true){
        int clientFD = accept(serverFD, nullptr, nullptr);
        

        if(clientFD == -1){
            perror("Accept error");
            return 1;
        }

        pid_t clientP = fork();

        if(clientP < 0){
            perror("Client fork");
            close(clientFD);
        }else if(clientP == 0){
            close(serverFD);
            handleClient(clientFD);
            return 0;
        }else{
            close(clientFD);
            while (waitpid(clientP, nullptr, WNOHANG) > 0) {};
        }
    }

}
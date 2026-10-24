#pragma once

#include <sqlite3.h>
#include <string>


using namespace std;

enum class userRoles{
    Customer = 1,
    Employee = 2,
    Manager = 3,
    Administrator = 4,
};

struct authRes{
    bool success = false;
    int userID = -1;
    userRoles role = userRoles::Customer;
    string message;
};

class authService{
    public:
        explicit authService(sqlite3* db);

        authRes authenticate(const string &username, const string &password, int role);
        authRes registerCustomer(const string& username, const string& password);
    
    private :
        sqlite3* db_;
};  


#include "includes/password_utils.hpp"

#include <sodium.h>
#include <stdexcept>

using namespace std;

string hashPassword(const string & password){
    if(password.empty()){
        throw invalid_argument("Password cannot be empty");
    }

    char hashedPassword[crypto_pwhash_STRBYTES];

    int result = crypto_pwhash_str(
        hashedPassword,
        password.c_str(),
        static_cast<unsigned long long>(password.size()),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    );

    if(result != 0){
        throw runtime_error("Password hasing failed");
    }

    return string(hashedPassword);
}

bool verifyPassword(const string & storedHash, const string & password){
    if (storedHash.empty() ||
        storedHash.size() >= crypto_pwhash_STRBYTES ||
        storedHash.find('\0') != string::npos) {
        return false;
    }

    int result = crypto_pwhash_str_verify(
        storedHash.c_str(),
        password.c_str(),
        static_cast<unsigned long long>(password.size())
    );

    return result == 0;
}
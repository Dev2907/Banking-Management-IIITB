#pragma once
#include <string>

using namespace std;

string hashPassword(const string& password);

bool verifyPassword(
    const string & storedHash,
    const string & password
);


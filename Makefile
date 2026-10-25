# Banking Management System (Linux / POSIX)
# Run all commands from the repository root.

.DEFAULT_GOAL := all

CXX      ?= g++
CPPFLAGS += -Isrc/server/includes -Isrc/server
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
LDFLAGS  ?=
LDLIBS   += -lsqlite3 -lsodium

DB_PATH     := src/db/main.db
SCHEMA_PATH := src/db/create_db_schema.sql

SERVER_SOURCES := \
	src/server/server.cpp \
	src/server/auth_service.cpp \
	src/server/password_utils.cpp \
	src/server/customer_portal.cpp \
	src/server/employee_portal.cpp \
	src/server/manager_portal.cpp \
	src/server/administrator_portal.cpp

SERVER_HEADERS := $(wildcard src/server/includes/*.hpp)

CLIENT_SOURCE := src/client/client.cpp
SEED_SOURCE   := src/db/seed_user_utility.cpp
PASSWORD_SOURCE := src/server/password_utils.cpp

.PHONY: all clean init-db seed run-server run-client help

# Build all executables, including the development seed utility.
all: bank_server bank_client seed_user_utility

bank_server: $(SERVER_SOURCES) $(SERVER_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(SERVER_SOURCES) $(LDLIBS) -o $@

bank_client: $(CLIENT_SOURCE)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CLIENT_SOURCE) -o $@

# The seed utility uses the same password hashing implementation as the server.
# Its source is in src/db/seed_user_utility.cpp (not tests/).
seed_user_utility: $(SEED_SOURCE) $(PASSWORD_SOURCE) src/server/includes/password_utils.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) \
		$(SEED_SOURCE) $(PASSWORD_SOURCE) $(LDLIBS) -o $@

# DESTRUCTIVE: create_db_schema.sql drops and recreates the banking tables.
# Back up src/db/main.db first if it contains data that must be preserved.
init-db:
	@command -v sqlite3 >/dev/null 2>&1 || { echo "Error: sqlite3 CLI is required (install the sqlite3 package)."; exit 1; }
	@mkdir -p src/db src/locks
	@echo "WARNING: this schema script drops and recreates existing banking tables."
	sqlite3 $(DB_PATH) < $(SCHEMA_PATH)
	@echo "Database schema initialized at $(DB_PATH)."

# Initialize the schema with `make init-db` before running `make seed`.
# Seeding is explicit; `make all` only compiles the utility and does not run it.
seed: seed_user_utility
	@test -f $(DB_PATH) || { echo "Error: $(DB_PATH) does not exist. Run 'make init-db' first."; exit 1; }
	./seed_user_utility

# These run from the repository root because DB and lock paths are relative.
run-server: bank_server
	./bank_server

run-client: bank_client
	./bank_client

clean:
	rm -f bank_server bank_client seed_user_utility

help:
	@echo "Targets:"
	@echo "  make all            Build bank_server, bank_client, and seed_user_utility (default)"
	@echo "  make bank_server    Build the server only"
	@echo "  make bank_client    Build the client only"
	@echo "  make seed_user_utility  Build the development seed utility only"
	@echo "  make init-db        Create/recreate the SQLite schema (DESTRUCTIVE)"
	@echo "  make seed           Insert/update development users and sample data"
	@echo "  make run-server     Build and run the server from the repository root"
	@echo "  make run-client     Build and run the client from the repository root"
	@echo "  make clean          Remove compiled executables"

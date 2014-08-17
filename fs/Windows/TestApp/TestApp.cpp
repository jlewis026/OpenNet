// TestApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include "sqlite3.h"
#include <time.h>
#include <sstream>




using namespace std;
int _tmain(int argc, _TCHAR* argv[])
{

	sqlite3* db;
	sqlite3_open("testdb.db", &db);
	const char* evaluated;
	std::string sql = "CREATE TABLE IF NOT EXISTS Files (FileName varchar(256) NOT NULL PRIMARY KEY, FileContents varbinary(1048576))";
	sqlite3_stmt* command;
	sqlite3_prepare_v2(db,sql.data(),sql.size(),&command,&evaluated);
	int val;
	while ((val = sqlite3_step(command)) != SQLITE_DONE) {

	}
	sqlite3_reset(command);
	sql = "INSERT INTO Files VALUES (?, ?)";
	sqlite3_prepare_v2(db, sql.data(), sql.size(), &command, &evaluated);
	for (int i = 200; i < 300; i++) {
		unsigned char* data = new unsigned char[1024 * 1024];
		//Insert 30 files
		std::stringstream mval;
		mval << i;
		std::string mstr = mval.str();
		sqlite3_bind_text(command, 1, mstr.data(), mstr.size(),0);
		sqlite3_bind_blob(command, 2, data, 1024 * 1024, 0);
		while ((val = sqlite3_step(command)) != SQLITE_DONE) {
		
		}

		sqlite3_reset(command);
		sqlite3_clear_bindings(command);
	}
	return 0;
}


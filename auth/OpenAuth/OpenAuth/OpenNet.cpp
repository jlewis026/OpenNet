#include "OpenAuth.h"
#include "sqlite3.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
std::string string_to_hex(const unsigned char* input, size_t len)
{
	static const char* const lut = "0123456789ABCDEF";

	std::string output;
	output.reserve(2 * len);
	for (size_t i = 0; i < len; ++i)
	{
		const unsigned char c = input[i];
		output.push_back(lut[c >> 4]);
		output.push_back(lut[c & 15]);
	}
	return output;
}

std::string hex_to_string(const std::string& input)
{
	static const char* const lut = "0123456789ABCDEF";
	size_t len = input.length();
	if (len & 1) throw std::invalid_argument("odd length");

	std::string output;
	output.reserve(len / 2);
	for (size_t i = 0; i < len; i += 2)
	{
		char a = input[i];
		const char* p = std::lower_bound(lut, lut + 16, a);
		if (*p != a) throw std::invalid_argument("not a hex digit");

		char b = input[i + 1];
		const char* q = std::lower_bound(lut, lut + 16, b);
		if (*q != b) throw std::invalid_argument("not a hex digit");

		output.push_back(((p - lut) << 4) | (q - lut));
	}
	return output;
}




class IDisposable {
public:
	virtual ~IDisposable(){};
};
class Certificate:public IDisposable {
public:
	std::vector<unsigned char> PublicKey;
	std::vector<unsigned char> PrivateKey;
	std::string Authority;
	std::map<std::string, std::vector<unsigned char>> Properties;
	std::vector<unsigned char> SignProperties() {
		std::vector<unsigned char> retval;
		//Message at beginning; signature at end
		size_t pos = 0;
		for (auto it = Properties.begin(); it != Properties.end(); it++) {
			//Write property name
			retval.resize(retval.size() + (sizeof(uint32_t)+it->first.size()));
			memcpy((unsigned char*)retval.data() + pos, it->first.data(), it->first.size());
			pos += retval.size() + (sizeof(uint32_t) + it->first.size());
			//Write property value
			retval.resize(retval.size() + (sizeof(uint32_t) + it->second.size()));
			memcpy((unsigned char*)retval.data() + pos, it->second.data(), it->second.size());
			pos += retval.size() + (sizeof(uint32_t) + it->second.size());
		}
		size_t sigsegv = CreateSignature(retval.data(), retval.size(), 0);
		size_t oldsz = retval.size();
		retval.resize(retval.size() + sigsegv);
		sigsegv = CreateSignature(retval.data(), oldsz, (unsigned char*)retval.data() + oldsz);
		return retval;
	}
};
class KeyDatabase:public IDisposable {
public:
	sqlite3* db;
	sqlite3_stmt* command_addcert;
	sqlite3_stmt* command_addobject;
	sqlite3_stmt* command_addkey;
	sqlite3_stmt* command_findcert;
	bool AddCertificate(Certificate* cert) {
		void* hash = CreateHash();
		UpdateHash(hash, cert->PublicKey.data(), cert->PublicKey.size());
		//Use Unsigned Charizard's thumbprint
		unsigned char izard[20];
		FinalizeHash(hash, izard);
		//Verify authority (if exists)
		if (cert->Authority.size() != 0) {
			//TODO: Search by authority
			printf("TODO: AUTHORITY SCAN NOT YET IMPLEMENTED\n");
			return false;
		}
		
		std::string thumbprint = string_to_hex(izard,20);
		sqlite3_bind_text(command_addcert, 1, thumbprint.data(), thumbprint.size(), 0);
		sqlite3_bind_blob(command_addcert, 2, cert->PublicKey.data(), cert->PublicKey.size(),0);
		sqlite3_bind_text(command_addcert, 3, cert->Authority.data(), cert->Authority.size(), 0);
		//Insert ificate
		int val;
		while ((val = sqlite3_step(command_addcert)) != SQLITE_DONE){ if (val != SQLITE_DONE)break; };
		
		sqlite3_reset(command_addcert);
		if (val == SQLITE_DONE) {
			return true;
		}
		return false;
	}
	Certificate* FindCertificate(const std::string& thumbprint) {
		sqlite3_bind_text(command_findcert, 1, thumbprint.data(), thumbprint.size(), 0);
		int val;
		Certificate* retval = 0;
		while ((val = sqlite3_step(command_findcert)) != SQLITE_DONE) {
			if (val == SQLITE_ROW) {
				retval = new Certificate();
				retval->PublicKey.resize(sqlite3_column_bytes(command_findcert, 1));
				memcpy(retval->PublicKey.data(), sqlite3_column_blob(command_findcert, 1),retval->PublicKey.size());
				retval->Authority = (const char*)sqlite3_column_text(command_findcert, 2);
				
			}
		}
	}
	KeyDatabase() {
		sqlite3_open("keydb.db", &db);
		char* err;
		//The thumbprint is a hash of the public key
		sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS Certificates (Thumbprint TEXT, PublicKey BLOB, Authority TEXT, SignedAttributes BLOB PRIMARY KEY(Thumbprint))",0,0,&err);
		sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS NamedObjects (Name TEXT, Authority TEXT, SignedData BLOB)", 0, 0, &err);
		sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS PrivateKeys (Thumbprint TEXT, PrivateKey BLOB)",0,0,&err);
		std::string sql = "INSERT INTO Certificates VALUES (?, ?, ?, ?)";
		const char* parsed;
		sqlite3_prepare(db, sql.data(), (int)sql.size(), &command_addcert, &parsed);
		sql = "INSERT INTO NamedObjects VALUES (?, ?, ?)";
		sqlite3_prepare(db, sql.data(), (int)sql.size(), &command_addobject, &parsed);
		sql = "INSERT INTO PrivateKeys VALUES (?, ?)";
		sqlite3_prepare(db, sql.data(), (int)sql.size(), &command_addkey, &parsed);
		sql = "SELECT * FROM Certificates WHERE Thumprint = ?";
		sqlite3_prepare(db, sql.data(), (int)sql.size(), &command_findcert, &parsed);


	}
	~KeyDatabase() {
		sqlite3_finalize(command_addcert);
		sqlite3_close(db);
	}
};

extern "C" {
	void* Initialize() {
		return new KeyDatabase();
	}
}
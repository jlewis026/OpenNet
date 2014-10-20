#include "OpenAuth.h"
#include "sqlite3.h"
#include <string.h>
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
class SafeResizableBuffer {
public:
	//This is a car
	unsigned char* buffer;
	size_t sz;
	SafeResizableBuffer() {
		buffer = 0;
		position = 0;
	}
	template<typename T>
	void Write(const T& value) {
		Write(&value,sizeof(value));
	}
	template<typename T>
	int Read(T& value) {
		return Read(&value,sizeof(value));
	}
	size_t position;
	int Read(void* buffer, int count) {
		if (position + count > sz) {
			//BARFIZARD!
			throw "up";
		}
		memcpy(buffer, this->buffer + position, count);
		position += count;
		return count;
	}
	void Write(const void* buffer, int count) {
		if (position + count > sz) {
			size_t allocsz = std::max(position + count, sz * 2);
			unsigned char* izard = new unsigned char[allocsz];
			memcpy(izard,this->buffer,sz);
			delete[] this->buffer;
		}
		memcpy(this->buffer, buffer, count);
	}
	~SafeResizableBuffer() {
		if(buffer) {
		delete[] buffer;
		}
	}
};
class SafeBuffer {
public:
	void* ptr;
	size_t sz;
	int Read(unsigned char* buffer, int count) {
		if (pos + count > sz) {
			throw "up";
		}

		memcpy(buffer, ((unsigned char*)ptr) + pos, count);

		pos += count;
		return count;
	}
	void Write(unsigned char* data, int count) {
		if (pos + count > sz) {
			throw "up";
		}
		memcpy(((unsigned char*)ptr) + pos, data, count);

		pos += count;
	}
	size_t pos;
	int64_t GetLength() {
		return (int64_t)sz;
	}
	template<typename T>
	void Read(T& val) {
        Read((unsigned char*)&val, sizeof(val));
	}
	template<typename T>
	void Write(const T& val) {
        Write((unsigned char*)&val, sizeof(val));
	}
	SafeBuffer(void* ptr, size_t sz) {
		this->ptr = ptr;
		this->sz = sz;
		pos = 0;
	}
};
class Certificate:public IDisposable {
public:
	std::vector<unsigned char> PublicKey;
	std::vector<unsigned char> PrivateKey;
	std::string Authority;
	std::map<std::string, std::vector<unsigned char>> Properties;
	std::vector<unsigned char> SignProperties() {
		//Max size limit is stupid
		
		SafeResizableBuffer s;
		//Message at beginning; signature at end
		uint32_t ct = (uint32_t)Properties.size();
		s.Write(ct);
		for (auto it = Properties.begin(); it != Properties.end(); it++) {
			uint32_t sz = it->first.size();
			s.Write(sz);
			s.Write((unsigned char*)it->first.data(),(int)it->first.size());
			sz = it->second.size();
			s.Write(sz);
			s.Write(it->second.data(), (int)it->second.size());
		}

		size_t sigsegv = CreateSignature((const unsigned char*)s.buffer, s.sz, 0);
		size_t oldsz = s.sz;
		std::vector<unsigned char> retval;
		retval.resize(oldsz + sigsegv);
		memcpy(retval.data(), s.buffer, oldsz);
		sigsegv = CreateSignature((unsigned char*)retval.data(), oldsz, (unsigned char*)retval.data() + oldsz);
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
				//TODO: Deserialize properties from verified blob
				SafeBuffer s((void*)sqlite3_column_blob(command_findcert, 3), sqlite3_column_bytes(command_findcert, 3));
				uint32_t sz;
				s.Read(sz);
				for (uint32_t i = 0; i < sz; i++) {
					uint32_t sz;
					s.Read(sz);
					char propname[256];
					if (sz > 256) {
						throw "up";
					}
					s.Read((unsigned char*)propname,sz);
					s.Read(sz);
					
				}
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

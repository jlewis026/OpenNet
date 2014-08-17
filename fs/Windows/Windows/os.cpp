#include "stdafx.h"
#include "OpenNet.h"
#include <Windows.h>
#include <wincrypt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <rpc.h>
#include "sqlite3.h"
#include <math.h>
namespace OpenNet {
	template<typename T = System::Object^>
	class GCPointer {
	public:
		bool hasValue;
		System::Runtime::InteropServices::GCHandle handle;
		GCPointer(T value) {
			handle = System::Runtime::InteropServices::GCHandle::Alloc(value);
			hasValue = true;
		}
		GCPointer() {
			hasValue = false;
		}
		GCPointer& operator=(T other) {
			if (hasValue) {
				delete handle.Target;
				handle.Free();
			}
			hasValue = true;
			handle = System::Runtime::InteropServices::GCHandle::Alloc(other);
			return *this;
		}
		T operator->() {
			System::Object^ target = handle.Target;
			return (T)target;
		}
		~GCPointer() {
			if (hasValue) {
				delete handle.Target;
				handle.Free();
			}
		}
	};
	class FileStream :public Stream {
	public:
		GCPointer<System::IO::Stream^> managedStream;
		FileStream(System::IO::Stream^ file) {
			managedStream = file;
		}
		int Read(int64_t position, unsigned char* buffer, int count) {
			managedStream->Position = position;
			array<unsigned char>^ mray = gcnew array<unsigned char>(count);
			count = managedStream->Read(mray, 0, count);
			pin_ptr<unsigned char> ptr = &mray[0];
			memcpy(buffer, ptr, count);
			return count;
		}
		void Write(int64_t position, unsigned char* data, int count) {
			managedStream->Position = position;
			array<unsigned char>^ mray = gcnew array<unsigned char>(count);
			pin_ptr<unsigned char> ptr = &mray[0];
			memcpy(ptr, data, count);
			managedStream->Write(mray, 0, count);
		}
		int64_t GetLength() {
			return managedStream->Length;
		}
		~FileStream(){
		
		};
	};
	class WinFS :public IFilesystem {
	public:
		std::string rootDir;
		WinFS(const std::string& rootDir) {
			this->rootDir = rootDir;
			
		}
		Stream* GetStream(const std::string& filename) {
			std::string realname = JoinFileComponents(GetFileComponents(filename),rootDir,"\\");
			return new FileStream(System::IO::File::Open(gcnew System::String(realname.data()),System::IO::FileMode::Open));
		}
		void Mknod(const std::string& path, bool isFile) {
			std::string realname = JoinFileComponents(GetFileComponents(path), rootDir, "\\");
			if (isFile) {
				delete System::IO::File::Create(gcnew System::String(realname.data()));

			}
			else {
				System::IO::Directory::CreateDirectory(gcnew System::String(realname.data()));
			}
		}
		void Stat(const std::string& filename,FileInformation& info) {
			std::string realname = JoinFileComponents(GetFileComponents(filename), rootDir, "\\");
			struct stat istics;
			stat(filename.data(), &istics);
			info.created = istics.st_ctime;
			info.lastModified = istics.st_mtime;
			memcpy(info.name, filename.data(), filename.size());
			info.name[filename.size()] = 0;
			info.size = istics.st_size;
			
		}
		bool Exists(const std::string& filename) {
			std::string realname = JoinFileComponents(GetFileComponents(filename), rootDir, "\\");
			DWORD doesExist = GetFileAttributesA(realname.data());
			return doesExist != INVALID_FILE_ATTRIBUTES;

		}
	};
	ref class EncryptionEngine {
	public:
		System::Security::Cryptography::RijndaelManaged^ engine;
		System::Security::Cryptography::RijndaelManagedTransform^ encryptor;
		System::Security::Cryptography::RijndaelManagedTransform^ decryptor;
		EncryptionEngine(System::Security::Cryptography::RijndaelManaged^ rdale) {
			engine = rdale;
			encryptor = (System::Security::Cryptography::RijndaelManagedTransform^)engine->CreateEncryptor();
			decryptor = (System::Security::Cryptography::RijndaelManagedTransform^)engine->CreateDecryptor();
		}
		~EncryptionEngine() {
			delete engine;
			delete encryptor;
			delete decryptor;
		}
	};



	//NOTE: Potentially cross-platform
	class DBStream:public Stream {
	public:
		unsigned char buffer[1024 * 1024];
		std::string name;
		sqlite3* db;
		sqlite3_stmt* updateCmd;
		sqlite3_stmt* readCmd;
		size_t sz;
		int64_t GetLength() {
			return sz;
		}
		int Read(int64_t position, unsigned char* buffer, int count) {
			if (position > sz) {
				return 0;
			}
			int available = (int)(min(count, sz - position));
			memcpy(buffer, this->buffer + position, available);
			return available;
		}
		void Write(int64_t position, unsigned char* buffer, int count) {
			const char* up = "down";
			if (position + count > sz) {
				sz = position + count;
				if (sz > 1024 * 1024) {
					//Barf
					throw up;
				}
			}
			memcpy(this->buffer + position, buffer, count);
		}
		~DBStream() {
			//Update file
			//UPDATE Files SET data = ?, LastModified = ? WHERE FileName = ?
			//File name, file type, created, modified, data
			int res = sqlite3_bind_blob(updateCmd, 1, buffer, sz, 0);
			time_t m;
			time(&m);
			res = sqlite3_bind_int64(updateCmd, 2, m);
			res = sqlite3_bind_text(updateCmd, 3, name.data(), name.size(),0);
			int val;
			while ((val = sqlite3_step(updateCmd)) != SQLITE_DONE) {

			}
			int rowsAffected = sqlite3_changes(db);
			if (rowsAffected == 0) {
				printf("Update fault\n");
			}
			sqlite3_reset(updateCmd);
			sqlite3_clear_bindings(updateCmd);
		}
	};
	class DBFS :public IFilesystem {
	public:
		sqlite3* db;
		sqlite3_stmt* insert;
		sqlite3_stmt* readfile;
		sqlite3_stmt* updatefile;
		void Stat(const std::string& filename, FileInformation& info) {
			sqlite3_bind_text(readfile, 1, filename.data(), filename.size(), 0);

			//File name, file type, created, modified, data
			while (true) {
				int val = sqlite3_step(readfile);
				if (val == SQLITE_ROW) {
					memcpy(info.name, filename.data(), filename.size());
					info.name[filename.size()] = 0;
					info.created = sqlite3_column_int64(readfile, 2);
					info.lastModified = sqlite3_column_int64(readfile, 3);
					info.size = sqlite3_column_bytes(readfile, 4);
					info.isFile = sqlite3_column_int(readfile, 1) !=0;
					return;
				}
			}
		}
		void Mknod(const std::string& path, bool isFile) {
			//File name, file type, created, modified, data
			time_t mt;
			time(&mt);
			sqlite3_bind_text(insert, 1, path.data(), path.size(), 0);
			sqlite3_bind_int(insert, 2, isFile);
			sqlite3_bind_int64(insert, 3, mt);
			sqlite3_bind_int64(insert, 4, mt);
			//Data is NULL (0 bytes)
			int val;
			while ((val = sqlite3_step(insert)) != SQLITE_DONE) {
				
			}
			sqlite3_reset(insert);
			sqlite3_clear_bindings(insert);

		}
		bool Exists(const std::string& filename) {
			sqlite3_bind_text(readfile, 1, filename.data(), filename.size(), 0);
			int val;
			while ((val = sqlite3_step(readfile)) != SQLITE_DONE) {
				if (val == SQLITE_ROW) {
					sqlite3_reset(readfile);
					sqlite3_clear_bindings(readfile);
					return true;
				}
			}
			sqlite3_reset(readfile);
			sqlite3_clear_bindings(readfile);
			return false;
		}

		Stream* GetStream(const std::string& path) {
			//Query for file
			sqlite3_bind_text(readfile, 1, path.data(), path.size(), 0);
			while (true) {
				int val = sqlite3_step(readfile);
				if (val = SQLITE_ROW) {
					//We've found our file!
					DBStream* retval = new DBStream(); 
					int coltype = sqlite3_column_type(readfile, 4);
					const void* buffy = sqlite3_column_blob(readfile, 4);
					int bytecount = sqlite3_column_bytes(readfile, 4);
					memcpy(retval->buffer, buffy, bytecount);
					retval->sz = bytecount; 
					retval->db = db;
					retval->name = path;
					retval->readCmd = readfile;
					retval->updateCmd = updatefile;
					sqlite3_reset(readfile);
					sqlite3_clear_bindings(readfile);
					return retval;
				}
			}
		}
		DBFS() {
			sqlite3_open("fs", &db);
			std::string sql = "CREATE TABLE IF NOT EXISTS Files (FileName varchar(256) NOT NULL PRIMARY KEY, fileType INTEGER, Created DATETIME,LastModified DATETIME, data varbinary(1048576))";
			sqlite3_stmt* command;
			const char* parsed;
			int status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &command, &parsed);
			int val = 0;
			while ((val = sqlite3_step(command)) != SQLITE_DONE) {

			}
			//File create command
			//File name, file type, created, modified, data
			sql = "INSERT INTO Files VALUES (?, ?, ?, ?, ?)";
			status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &insert, &parsed);
			//Read data from file
			sql = "SELECT * FROM Files WHERE FileName = ?";
			status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &readfile, &parsed);
			//Update file
			sql = "UPDATE Files SET data = ?, LastModified = ? WHERE FileName = ?";
			status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &updatefile, &parsed);

		}
		~DBFS() {
			sqlite3_close(db);
		}
	};



	//END cross-platform


	extern "C" {
		EXPORT void OS_MkGuid(char output[256]) {
			GUID mg;
			UuidCreate(&mg);
			RPC_CSTR mstr;
			UuidToStringA(&mg, &mstr);
			memset(output, 0, 256);
			memcpy(output, mstr, strlen((char*)mstr));
			RpcStringFreeA(&mstr);

		}
		EXPORT void OS_ParseGuid(const char* input, unsigned char output[16]) {
			GUID mg;
			RPC_STATUS mstat = UuidFromStringA((unsigned char*)input, &mg);
			if (mstat != RPC_S_OK) {
				throw "WAY DOWN";
			}
			memcpy(output, &mg, 16);
		}
		//Converts a GUID to a string
		EXPORT void OS_GuidToString(const unsigned char input[16], char output[256]) {
			GUID mg;
			memcpy(&mg, input, 16);
			RPC_CSTR mstr;
			UuidToStringA(&mg, &mstr);
			memset(output, 0, 256);
			memcpy(output, mstr, strlen((char*)mstr));
			RpcStringFreeA(&mstr);

		}

		EXPORT void* OS_AES_GetKeyHandle(unsigned char key[32]) {
			//Managed code with Robbinsdale seems to be the only way to do this on Windows.....
			System::Security::Cryptography::RijndaelManaged^ rdale = gcnew System::Security::Cryptography::RijndaelManaged();
			rdale->Mode = System::Security::Cryptography::CipherMode::ECB;
			rdale->Padding = System::Security::Cryptography::PaddingMode::None;
			auto mray = gcnew array<unsigned char>(32);
			pin_ptr<unsigned char> ptr = &mray[0];
			memcpy(ptr, key, 32);
			rdale->Key = mray;
			GCPointer<EncryptionEngine^>* retval = new GCPointer<EncryptionEngine^>(gcnew EncryptionEngine(rdale));
			return retval;
		}
		static inline void xorblock(unsigned char* data, unsigned char* ivfluid) {
			for (size_t i = 0; i < 16; i++) {
				data[i] ^= ivfluid[i];
			}
		}
		EXPORT void OS_Aes_Encrypt(void* key, unsigned char* data, size_t sz, unsigned char iv[16]) {
			//printf("CRYPT-NO-OP\n");
			//return;
			GCPointer<EncryptionEngine^>& engine = *(GCPointer<EncryptionEngine^>*)key;
			array<unsigned char>^ mray = gcnew array<unsigned char>(16);
			pin_ptr<unsigned char> ptr = &mray[0];
			unsigned char fluid[16];
			//Create the IV fluid
			memcpy(fluid, iv, 16);
			for (size_t i = 0; i < sz; i+=16) {
				memcpy(ptr, data+i, 16);
				//Replace the IV fluid with pointer fluid.
				xorblock(ptr, fluid);
				//Transform the data using the encryption algorithm
				engine->encryptor->TransformBlock(mray, 0, 16, mray, 0);
				//Replace IV fluid
				memcpy(fluid, ptr, 16);
				//Output (final) encrypted data
				memcpy(data + i, ptr, 16);
			}

		}
		EXPORT void OS_Aes_Decrypt(void* keyHandle, unsigned char* data, size_t sz, unsigned char iv[16]) {
			//printf("CRYPT-NO-OP\n");
			//return;
			//Decrypt data
			GCPointer<EncryptionEngine^>& engine = *(GCPointer<EncryptionEngine^>*)keyHandle;
			array<unsigned char>^ buffy = gcnew array<unsigned char>(16);
			pin_ptr<unsigned char> ptr = &buffy[0];
			unsigned char fluid[16];
			memcpy(fluid, iv, 16);
			for (size_t i = 0; i < sz; i += 16) {
				//Begin cyphertext transformation
				memcpy(ptr, data + i, 16);
				engine->decryptor->TransformBlock(buffy, 0, 16, buffy, 0);
				//Transform by IV
				xorblock(ptr, fluid);
				//Replace the IV fluid
				memcpy(fluid, data + i, 16);
				//Output plaintext
				memcpy(data + i, ptr, 16);
			}
		}
		EXPORT void OS_GetFilesystem(Filesystem* fsptr) {
			char buffer[MAX_PATH];
			memset(buffer, 0, sizeof(buffer));
			GetCurrentDirectoryA(MAX_PATH, buffer);

			//WinFS* winfs = new WinFS(buffer);
			DBFS* winfs = new DBFS();
			ManagedToNativeFS(winfs, fsptr);
		}
	}
}
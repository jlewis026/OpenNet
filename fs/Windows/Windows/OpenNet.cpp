// Windows.cpp : Defines the exported functions for the DLL application.
//
#include "OpenNet.h"
#include <algorithm>
#include "sqlite3.h"
#include <string>
#include <map>
class Exception:public std::exception {
public:
	std::string msg;
	Exception(const std::string& msg) {
		this->msg = msg;
	}
};
namespace OpenNet {
	namespace Internal {
	class Block {
	public:
		unsigned char data[DISTFS_BLOCK_SIZE];
		Block() {

		}
		Block(unsigned char* ptr) {
			memcpy(data,ptr,DISTFS_BLOCK_SIZE);
		}
	};
	class BlockCache {
	public:
		std::map<std::string,Block> cachedBlocks;
		bool isInCache(const std::string& blockID) {
			return false;
			bool retval = cachedBlocks.find(blockID) != cachedBlocks.end();
			return retval;
		}
		void updateBlock(const std::string& name,unsigned char data[DISTFS_BLOCK_SIZE]) {
			cachedBlocks[name] = data;
		}
	};
	static BlockCache cache;
		//Database section
		static int dbopen(sqlite3_vfs* fs, const char *zName, sqlite3_file* mfile, int flags, int *pOutFlags);
		static int dbdelete(sqlite3_vfs* fptr, const char *zName, int syncDir);
		static int dbaccess(sqlite3_vfs*, const char *zName, int flags, int *pResOut) {
			if (flags == SQLITE_ACCESS_EXISTS) {
				std::string rname = zName;
				if (rname == "root.db" || rname == "root.db-journal") {
					*pResOut =1;
				}
				else {
					*pResOut = 0;
				}
				return 0;
			}
			throw "down";
		}static int getfullname(sqlite3_vfs*, const char *zName, int nOut, char *zOut) {
			memcpy(zOut, zName, 257);
			return 0;
		}
		static int dorand(sqlite3_vfs* v, int nByte, char* prand) {
			for (int i = 0; i < nByte; i++) {
				prand[i] = rand();
			}
			return 0;
		}
		static Stream* openEncryptedStream(Stream* firstBlock, unsigned char initialID[16], void* key, IFilesystem* nativeFS);
		class DistFS :public IFilesystem {
		public:
			IFilesystem* underlyingFS;
			Stream* rootDB;
			Stream* rootJournal;
			void* key;
			sqlite3* db;
			sqlite3_stmt* insertcommand;
			sqlite3_stmt* findCommand;
			sqlite3_stmt* enumerateCommand;
			~DistFS() {
				delete rootDB;
				delete rootJournal;
			}
			void EnumerateNodes(const std::string& path, std::function<bool(const FileInformation&)> callback) {
				//For now; just assume root inode (0)
				sqlite3_bind_int64(enumerateCommand,1,0);
				int val = 0;
				FileInformation info;
				while((val = sqlite3_step(enumerateCommand)) != SQLITE_DONE) {
					if(val == SQLITE_ROW) {
						info.isFile = sqlite3_column_int(enumerateCommand, 2) !=0;
						info.created = sqlite3_column_int64(enumerateCommand, 3);
						info.lastModified = sqlite3_column_int64(enumerateCommand, 4);
						std::string fname = (const char*)sqlite3_column_text(enumerateCommand,0);
						memcpy(info.name, fname.data(), fname.size());
						info.name[fname.size()] = 0;
						if(!callback(info)){
							break;
						}

					}
				}
				sqlite3_reset(enumerateCommand);
				sqlite3_clear_bindings(enumerateCommand);
			}
			Stream* GetStream(const std::string& path) {
				//FileName, parent, fileType, Created,LastModified, FilePointer, inode, revision, user, group, permissions
				sqlite3_bind_text(findCommand, 1, path.data(), path.size(), 0);
				//Root inode
				sqlite3_bind_int(findCommand,2,0);
				int val;
				while ((val = sqlite3_step(findCommand)) != SQLITE_DONE) {
					if (val == SQLITE_ROW) {
						const void* blockid = sqlite3_column_blob(findCommand, 5);
						char str[256];
						OS_GuidToString((const unsigned char*)blockid, str);
						//Open crypto-stream
						if (!underlyingFS->Exists(str)) {
							underlyingFS->Mknod(str, true);
						}
						Stream* retval = openEncryptedStream(underlyingFS->GetStream(str), (unsigned char*)blockid, key,underlyingFS);
						sqlite3_reset(findCommand);
						sqlite3_clear_bindings(findCommand);
						return retval;
					}
				}
				sqlite3_reset(findCommand);
				sqlite3_clear_bindings(findCommand);
				return 0;
			}
			bool Exists(const std::string& path) {
				
				sqlite3_bind_text(findCommand, 1, path.data(), path.size(), 0);
				//Root inode
				sqlite3_bind_int(findCommand,2,0);
				int val;
				while ((val = sqlite3_step(findCommand)) != SQLITE_DONE) {
					printf("%s\n",sqlite3_errmsg(db));
					if (val == SQLITE_ROW) {
						sqlite3_reset(findCommand);
						sqlite3_clear_bindings(findCommand);
						return true;
					}
				}
				sqlite3_reset(findCommand);
				sqlite3_clear_bindings(findCommand);
				return false;
			}
			void Stat(const std::string& path, FileInformation& info) {
				sqlite3_bind_text(findCommand, 1, path.data(), path.size(), 0);
				//Root inode
				sqlite3_bind_int(findCommand,2,0);
				int val;
				while ((val = sqlite3_step(findCommand)) != SQLITE_DONE) {
					if (val == SQLITE_ROW) {
						//FileName, parent, fileType, Created,LastModified, FilePointer, inode, revision, user, group, permissions
						info.isFile = sqlite3_column_int(findCommand, 2) !=0;
						info.created = sqlite3_column_int64(findCommand, 3);
						info.lastModified = sqlite3_column_int64(findCommand, 4);
						info.size = 0;
						if(info.isFile) {
						const void* blockid = sqlite3_column_blob(findCommand, 5);
						char str[256];
						OS_GuidToString((const unsigned char*)blockid, str);
						if(underlyingFS->Exists(str)) {
						Stream* encstr = openEncryptedStream(underlyingFS->GetStream(str),(unsigned char*)blockid,key,underlyingFS);
						info.size = encstr->GetLength();
						delete encstr;
						}
						}
						memcpy(info.name, path.data(), path.size());
						info.name[path.size()] = 0;
						sqlite3_reset(findCommand);
						sqlite3_clear_bindings(findCommand);
						return;
					}
				}
				sqlite3_reset(findCommand);
				sqlite3_clear_bindings(findCommand);
				throw Exception("IO Error");
			}
			int64_t currentInode;
			void Mknod(const std::string& path, bool isFile) {
				//FileName, parent, fileType, Created,LastModified, FilePointer, inode, revision, user, group, permissions
				sqlite3_bind_text(insertcommand, 1, path.data(), path.size(), 0);
				//INODE parent (root)
				sqlite3_bind_int64(insertcommand,2,0);
				sqlite3_bind_int(insertcommand, 3, isFile);
				time_t currentTime;
				time(&currentTime);
				sqlite3_bind_int64(insertcommand, 4, currentTime);
				sqlite3_bind_int64(insertcommand, 5, currentTime);
				//TODO: INODE number
				sqlite3_bind_int64(insertcommand,6,currentInode);

				//Create 16-byte GUID and bind with blob
				char output[256];
				OS_MkGuid(output);
				unsigned char realGuid[16];
				OS_ParseGuid(output, realGuid);
				sqlite3_bind_blob(insertcommand, 5, realGuid, 16, 0);
				//Update database
				int val;
				while ((val = sqlite3_step(insertcommand)) != SQLITE_DONE) {

				}
				sqlite3_reset(insertcommand);
				sqlite3_clear_bindings(insertcommand);
			}
			DistFS(void* keyHandle,Stream* encryptedDB, Stream* encryptedJournal) {
				rootDB = encryptedDB;
				key = keyHandle;
				rootDB = encryptedDB;
				rootJournal = encryptedJournal;
				struct sqlite3_vfs* mptr = new struct sqlite3_vfs();
				sqlite3_vfs& vfs = *mptr;
				vfs.iVersion = 3;
				vfs.szOsFile = sizeof(vfs);
				vfs.mxPathname = 256;
				vfs.zName = "IC80";
				vfs.xOpen = dbopen;
				vfs.xDelete = dbdelete;
				vfs.xAccess = dbaccess;
				vfs.xFullPathname = getfullname;
				vfs.xRandomness = dorand;
				vfs.pAppData = this;
				sqlite3_vfs_register(mptr, 1);
				
				sqlite3_open("root.db", &db);
				
				std::string sql = "CREATE TABLE IF NOT EXISTS Files (FileName varchar(256) NOT NULL, parent INTEGER, fileType INTEGER, Created DATETIME,LastModified DATETIME, FilePointer varbinary(16), inode INTEGER, revision INTEGER, userID INTEGER, groupID INTEGER, permissions INTEGER, PRIMARY KEY(FileName, parent))";
				sqlite3_stmt* command;
				const char* parsed;
				if (rootDB->GetLength() == 0){
					//sqlite3_exec(db,"BEGIN TRANSACTION;",0,0,0);
					int status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &command, &parsed);
					int val = 0;
					while ((val = sqlite3_step(command)) != SQLITE_DONE) {

						printf("%s\n",sqlite3_errmsg(db));
					}
					sqlite3_reset(command);
					sqlite3_finalize(command);
					//Free INODES table
					sql = "CREATE TABLE IF NOT EXISTS Inodes (inode INTEGER, isfree INTEGER, FilePointer varbinary(16), PRIMARY KEY(inode,isfree))";
					status = sqlite3_prepare_v2(db, sql.data(), sql.size(), &command, &parsed);
					val = 0;
					while ((val = sqlite3_step(command)) != SQLITE_DONE) {}
					//sqlite3_exec(db,"END TRANSACTION;",0,0,0);
				}
				//Filesystem is now mounted!
				//Now; for the hardcoded queries
				//FileName, parent, fileType, Created,LastModified, FilePointer, inode, revision, user, group, permissions
				sql = "INSERT INTO Files VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0, 0, 0)";
				sqlite3_prepare_v2(db, sql.data(), sql.size(), &insertcommand, &parsed);
				sql = "SELECT * FROM Files WHERE FileName = ? AND parent = ?";
				sqlite3_prepare_v2(db, sql.data(), sql.size(), &findCommand, &parsed);
				sql = "SELECT * FROM Files WHERE parent = ?";
				sqlite3_prepare_v2(db,sql.data(),sql.size(),&enumerateCommand,&parsed);
			}
			
		};

		typedef struct {
			struct sqlite3_io_methods methods;
			void* fileHandle;
		} VFSFile;
		static int dbsync(sqlite3_file* fptr, int code) {
			return 0;
		}
		static int dbcontrol(sqlite3_file* fptr, int op, void* arg) {
			return SQLITE_NOTFOUND;
		}
		static int dblock(sqlite3_file* fptr, int ltype) {
			return 0;
		}
		static int dbunlock(sqlite3_file* fptr, int code) {
			return 0;
		}
		static int dbcharacteristics(sqlite3_file* fptr) {

			return SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN | SQLITE_IOCAP_ATOMIC;
		}
		static int dbsize(sqlite3_file* fptr, sqlite3_int64* sz) {
			VFSFile* vfile = (VFSFile*)fptr->pMethods;
			Stream* mstream = (Stream*)vfile->fileHandle;
			*sz = mstream->GetLength();
			return 0;
		}
		static int dbwrite(sqlite3_file* fptr, const void* buffer, int iAmt, sqlite3_int64 iOfst) {

			VFSFile* vfile = (VFSFile*)fptr->pMethods;
			Stream* mstream = (Stream*)vfile->fileHandle;
			/*if (iOfst == 8192) {
				printf("The 8192 problem\n");
			}*/
			mstream->Write(iOfst, (unsigned char*)buffer, iAmt);
			
			return 0;
		}
		static int dbclose(sqlite3_file* fptr) {
			//DON'T dispose of actual file handle here.
			//Stream may need to be re-used in the near future.
			//so we will keep it until the underlying IFilesystem is disposed.
			VFSFile* mfile = (VFSFile*)fptr->pMethods;
			delete mfile;
			return 0;
		}
		static int dbIsLocked(sqlite3_file* fptr, int* lockStatus) {
			*lockStatus = 0;
			return 0;
		}
		static int dbread(sqlite3_file* fptr, void* buffer, int iAmt, sqlite3_int64 iOfst) {
			VFSFile* vfile = (VFSFile*)fptr->pMethods;
			Stream* mstream = (Stream*)vfile->fileHandle;
			/*if (iOfst == 20480) {
				printf("The 20480 bug\n");
				//CONFIRMED: Segmentation fault happens inside mstream->Read command
			}*/
			if (mstream->Read(iOfst, (unsigned char*)buffer, iAmt) == iAmt) {
				return 0;
			}
			return SQLITE_IOERR_SHORT_READ;
			
			//throw "horizontally";
		}
		static int dbtruncate(sqlite3_file* fptr, sqlite3_int64 sz) {
			VFSFile* vfile = (VFSFile*)fptr->pMethods;
			ExtendedStream* mstream = (ExtendedStream*)vfile->fileHandle;
			mstream->Truncate(sz);
			return 0;
		}
		static int dbopen(sqlite3_vfs* fs, const char *zName, sqlite3_file* mfile, int flags, int *pOutFlags) {
			DistFS* realfs = (DistFS*)fs->pAppData;
				struct sqlite3_io_methods* methods = (sqlite3_io_methods*)new VFSFile();
				methods->iVersion = 3;
				methods->xClose = dbclose;
				methods->xRead = dbread;
				methods->xWrite = dbwrite;
				methods->xTruncate = dbtruncate;
				methods->xDeviceCharacteristics = dbcharacteristics;
				methods->xFileControl = dbcontrol;
				methods->xLock = dblock;
				methods->xCheckReservedLock = dbIsLocked;
				methods->xFileSize = dbsize;
				methods->xUnlock = dbunlock;
				methods->xSync = dbsync;
				mfile->pMethods = methods;
				VFSFile* realptr = (VFSFile*)methods;
				if (std::string(zName) == "root.db") {
				realptr->fileHandle = realfs->rootDB;
			}
			else {
				realptr->fileHandle = realfs->rootJournal;
			}
			return 0;
		}
		static int dbdelete(sqlite3_vfs* fptr, const char *zName, int syncDir) {
			DistFS* realfs = (DistFS*)fptr->pAppData;
			((ExtendedStream*)realfs->rootJournal)->Truncate(0);
			return 0;
		}
		typedef struct {
			unsigned char value[16];
		} fileid;
		class EncryptedStream:public ExtendedStream {
		public:
			int64_t len;
			int64_t GetLength() {
				return len;
			}
			void Truncate(int64_t size) {
				if (size > len) {
					throw Exception("UnsupportedException");
				}
				len = size;
			}
			//The current raw block
			Stream* raw;
			//The initial block in the file
			Stream* initialBlock;
			//The platform-specific filesystem
			IFilesystem* underlyingFS;
			void* key;
			//The first block identifier
			unsigned char initialID[16];
			//The current block identifier
			unsigned char blockID[16];
			//A 1MB buffer -- used for numerous operations in the file (avoids frequent memory allocation/de-allocation cycles)
			unsigned char buffer[DISTFS_BLOCK_SIZE];
			//Cached block lookup table for 1MB file
			fileid lookuptable[((DISTFS_BLOCK_SIZE)/16)-16];


			size_t currentBlockSegment;
			void xoriv(unsigned char* dest, unsigned char* src) {
				for (int i = 0; i < 16; i++) {
					dest[i] |= src[i];
				}
			}
			void Encrypt() {
				unsigned char mid[16];
				memcpy(mid, blockID, 16);
				xoriv(mid, initialID);
				OS_Aes_Encrypt(key, buffer, sizeof(buffer), blockID);
			}
			void Decrypt() {
				unsigned char mid[16];
				memcpy(mid, blockID, 16);
				xoriv(mid, initialID);
				OS_Aes_Decrypt(key, buffer, sizeof(buffer), blockID);
			}
			unsigned char auxbuffer[DISTFS_BLOCK_SIZE];
			void StoreBlock() {
				char bname[256];
				OS_GuidToString(this->blockID,bname);
				cache.updateBlock(bname,buffer);

				memcpy(auxbuffer, buffer, sizeof(buffer));
				Encrypt();
				raw->Write(0, buffer);
				memcpy(buffer, auxbuffer, sizeof(buffer));

			}
			
			void LoadBlock(size_t blockID) {
				if (currentBlockSegment == blockID) {
					//Block is already in memory
					return;
				}
				currentBlockSegment = blockID;
				if (blockID < (sizeof(lookuptable) / sizeof(fileid))-2) {
					int64_t bt_i = GetLength()/sizeof(buffer);
					while (blockID >= (int)std::ceil(((double)GetLength() /(double)sizeof(buffer)))) {
						//Create blocks
						char output[256];
						OS_MkGuid(output);
						OS_ParseGuid(output, lookuptable[bt_i].value);
						underlyingFS->Mknod(output, true);
						raw = underlyingFS->GetStream(output);
						//Don't memset new blocks -- this will make it harder for attackers to analyze data.
						memset(buffer, 0, sizeof(buffer));
						memcpy(this->blockID, lookuptable[bt_i].value,16);
						Encrypt();
						raw->Write(0, buffer);
						bt_i++;
						this->len += sizeof(buffer);
					}
					//Convert the GUID to a string
					char str[256];
					memcpy(this->blockID, lookuptable[blockID].value, 16);
					OS_GuidToString(lookuptable[blockID].value, str);
					//Delete the previous blockstream
					if (raw != initialBlock) {
						delete raw;
					}
					//Open the new block and decrypt it
					if(!cache.isInCache(str)) {
					raw = underlyingFS->GetStream(str);
					raw->Read(0, buffer);
					Decrypt();
					cache.updateBlock(str,buffer);
					}else {
						memcpy(buffer,cache.cachedBlocks[str].data,DISTFS_BLOCK_SIZE);
					}

				}
				else {
					auto up(Exception("Auxillary blocks not yet implemented"));
					throw up;
				}
			}
			void Write(int64_t position, unsigned char* buffer, int count) {
				tableChanged = true;
				int64_t oldlen = len;
				int available = count;
				//Read from the file stream
				size_t bytesRead = 0;
				int64_t currentPosition = position;
				while (bytesRead < available) {
					//Compute current block ID
					size_t blockID = currentPosition / (DISTFS_BLOCK_SIZE);
					//Compute block offset (offset within current block)
					size_t blockOffset = currentPosition % (DISTFS_BLOCK_SIZE);
					//Compute data available in block
					size_t blockAvailable = (DISTFS_BLOCK_SIZE) - blockOffset;
					blockAvailable = std::min(blockAvailable, available - bytesRead);
					//Read block
					LoadBlock(blockID);
					//Update block
					memcpy(this->buffer + blockOffset, buffer + bytesRead, blockAvailable);
					//Save block
					StoreBlock();
					//Increment our current position and number of bytes read
					currentPosition += blockAvailable;
					bytesRead += blockAvailable;
				}
				if (currentPosition > oldlen) {
					//Set new file size
					len = currentPosition;
				}
			}
			int Read(int64_t position, unsigned char* buffer, int count) {
				if (position > len) {
					return 0;
				}
				int available = GetLength()-position;
				available = std::min(count, available);
				//Read from the file stream
				size_t bytesRead = 0;
				int64_t currentPosition = position;
				while (bytesRead < available) {
					//Compute current block ID
					size_t blockID = currentPosition / (DISTFS_BLOCK_SIZE);
					//Compute block offset (offset within current block)
					size_t blockOffset = currentPosition % (DISTFS_BLOCK_SIZE);
					//Compute data available in block
					size_t blockAvailable = (DISTFS_BLOCK_SIZE) - blockOffset;
					blockAvailable = std::min(blockAvailable, available-bytesRead);
					//Read block
					LoadBlock(blockID);

					memcpy(buffer+bytesRead, this->buffer + blockOffset, blockAvailable);
					//Increment our current position and number of bytes read
					currentPosition += blockAvailable;
					bytesRead += blockAvailable;
				}
				return bytesRead;
			}
			bool tableChanged;
			EncryptedStream(Stream* rawStream, IFilesystem* nativeFS, void* keyHandle, unsigned char initialBlockID[16]) {
				currentBlockSegment = -1;
				tableChanged = false;
				key = keyHandle;
				raw = rawStream;
				initialBlock = rawStream;
				underlyingFS = nativeFS;
				memcpy(blockID, initialBlockID, 16);
				memcpy(initialID, initialBlockID, 16);
				//Length stored as 16 bytes in 8-byte unsigned integer format le
				//Total Block Length = first 16 bytes (compressed in 8-byte unsigned int)
				//GUID block table = rest of initial block file+reserved auxillary entry
				//(without the reserved entry we would have a maximum filesize of approximately 64GB)

				if (rawStream->GetLength() == 0) {
					//Create inital block entry for file
					len = 0;
					memcpy(buffer, &len, 8);
					//Encrypt the block
					Encrypt();
					//Write the block to disk
					raw->Write(0, buffer);
					tableChanged = true;
				}
				//Read in the current block
				char name[256];
				OS_GuidToString(initialBlockID,name);
				if(!cache.isInCache(name)) {
				raw->Read(0, buffer);
				Decrypt();
				cache.updateBlock(name,buffer);
				}
				//Determine the file length
				memcpy(&len, buffer, 8);
				//Load the block lookup table
				memcpy(lookuptable, buffer + 16, sizeof(lookuptable));
			}
			~EncryptedStream() {
				//Write updated file table to disk
				if(tableChanged) {
				memcpy(blockID, initialID, 16);
				memcpy(buffer, &len, 8);
				memcpy(buffer + 16, lookuptable, sizeof(lookuptable));
				char name[256];
				OS_GuidToString(initialID,name);
				//cache.updateBlock(name,buffer);
				Encrypt();
				initialBlock->Write(0, buffer);
				}
				//Flush handles
				if (raw != initialBlock) {
					delete raw;
				}
				delete initialBlock;

			}
		};
		static Stream* openEncryptedStream(Stream* firstBlock, unsigned char initialID[16], void* key, IFilesystem* nativeFS) {
			//EncryptedStream(Stream* rawStream, IFilesystem* nativeFS, void* keyHandle, unsigned char initialBlockID[16]) {
			return new EncryptedStream(firstBlock, nativeFS, key, initialID);
		}







		extern "C" {
			
			EXPORT void OpenNet_MountFS(const Filesystem* mountPoint, Filesystem* mountedFS, char key[32]) {
				//Check for root pointer files
				
				srand((unsigned int)time(NULL));
				ABI_Filesystem* root = new ABI_Filesystem(*mountPoint);
				if (!root->Exists("rootptr")) {
					//Create root pointer file (this file should be un-encrypted (plaintext))
					root->Mknod("rootptr", true);
					Stream* mstr = root->GetStream("rootptr");
					char rootDB[256];
					char rootJournal[256];
					OS_MkGuid(rootDB);
					OS_MkGuid(rootJournal);
					root->Mknod(rootDB, true);
					root->Mknod(rootJournal, true);
					mstr->Write(0, rootDB);
					mstr->Write(256, rootJournal);
					delete mstr;
				}

				char db[256];
				char journal[256];
				unsigned char mdb[16];
				unsigned char mjournal[16];
				Stream* mstr = root->GetStream("rootptr");
				mstr->Read(0, db);
				mstr->Read(256, journal);
				OS_ParseGuid(db, mdb);
				OS_ParseGuid(journal, mjournal);
				delete mstr;
				void* keyHandle = OS_AES_GetKeyHandle((unsigned char*)key);
				DistFS* fs = new DistFS(keyHandle, openEncryptedStream(root->GetStream(db), mdb, keyHandle, root), openEncryptedStream(root->GetStream(journal), mdb, keyHandle, root));
				fs->underlyingFS = root;
				ManagedToNativeFS(fs, mountedFS);
			}
			EXPORT void OpenNet_Free(void* ptr) {
				IDisposable* obj = (IDisposable*)ptr;
				delete obj;
			}

		}
	}
}


#include <cmath>
#include <math.h>
#include <algorithm>
#include <string>
#include "OpenNet.h"
#include "MemoryAbstraction.h"
#include <fcntl.h>
#include "sqlite3.h"
#include <map>
using namespace OpenNet;


template<typename T>
static inline T ceildiv(const T& a, const T& b) {
    if(a %b) {
        return (a/b)+1;
    }
    return a/b;
}




typedef struct {
	//Whether or not the drive can be "cleanly" mounted
	bool dirtyBit;
	//The block size (default = 4kb)
	uint64_t blockSz;
	//Filesystem journal location (byte offset)
	uint64_t journal;
	//Partition size (logical size of filesystem, including journal, but excluding this volume header)
	uint64_t blockCount;
	//The length of the journal in bytes
	uint64_t journalSize;
} VolumeHeader;
typedef struct {
	//The size of the transaction (in logical blocks)
	uint64_t size;
	//The location of the transaction on disk (block number)
	uint64_t location;
	//Whether or not the system has started comitting this entry to disk
	//(note; only valid for initial blocks in a transaction)
	bool written;
	//The transaction ID of this block
	uint64_t transactionId;
} JournalEntry;

class PendingTransaction {
public:
	//Start location of transaction in bytes
	uint64_t startloc;
	//Length of transaction in bytes
	uint64_t length;
	PendingTransaction() {
		//Impending Doom
		startloc = 0;
		length = 0;
	}
};

//This Stream object provides for the following functionality
/*
 * Safe, block-level journaling and commit cycle guaranteeing the safety of all
 * write operations.
 * Facilities for coordinating transactions between different filesystem layers
 * Support for implementing a block-level encryption scheme
 * Support for logging changes to a volume
 * Support for partition-level metadata, including "dirty bit"
 * Support for non-journaled, volume "write-through" (if you like to live/die dangerously)
 */
class JournaledIoStream:public MemoryAbstraction::Stream {
public:
	MemoryAbstraction::Stream* dev;
	VolumeHeader header;

	JournaledIoStream(MemoryAbstraction::Stream* blockDevice, uint64_t blockSz, uint64_t blockCount) {
		dev = blockDevice;
		//Read first track
		blockDevice->Read(0,header);
		if(header.blockSz == 0) {
			header.blockSz = blockSz;
			header.blockCount = blockCount;
		}
		if(header.dirtyBit) {
			//Execute volume recovery, PAYBACK journal!
			//The number of transactions in the journal (journal size)
			uint64_t transactionCount;
			blockDevice->Read(header.journal,transactionCount);
			uint64_t currentPosition = header.journal+sizeof(transactionCount);
			JournalEntry transaction;
			uint64_t transactionID = 0;
            std::map<uint64_t,bool> safeTransactions;
			for(uint64_t i = 0;i<transactionCount;i++) {
				blockDevice->Read(currentPosition,transaction);
				currentPosition+=sizeof(transaction);
                if (transaction.written) {
                    safeTransactions[transaction.transactionId] = true;
                }
				if(safeTransactions[transaction.transactionId]) {
					//Write transaction to disk
					unsigned char* mray = new unsigned char[transaction.size*header.blockSz];
					blockDevice->Read(currentPosition,mray,transaction.size*header.blockSz);
					blockDevice->Write(transaction.location*header.blockSz,mray,transaction.size*header.blockSz);
					delete[] mray;
				}
				currentPosition+=transaction.size*header.blockSz;
			}
		}
		//Clear dirty bit; filesystem is SAFE for mounting a horse! (or a hearse)
		header.dirtyBit = false;
		blockDevice->Write(0,header);
		transactionSize = 0;
		maxID = 0;
	}
	void SetJournal(uint64_t location, uint64_t journalSize) {
		header.journal = location;
		header.journalSize = journalSize;
        header.dirtyBit = true;
		dev->Write(0,header);
	}
	std::recursive_mutex mtx;
	std::map<uint64_t,uint64_t> modified;
	//Pending transactions
	std::map<uint64_t,PendingTransaction> pendingTransactions;
	//The total size of all pending transactions
	uint64_t transactionSize;
	//The max ID for transactions
	uint64_t maxID;
	//The current transaction identifier
	uint64_t currentTransaction;
	void SetTransaction(uint64_t id) {
		currentTransaction = id;
	}
	uint64_t BeginTransaction() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
		uint64_t retval = maxID;
		currentTransaction = retval;
		maxID++;
        header.dirtyBit = true;
        dev->Write(0, header);
		return retval;
	}
	void EndTransaction(uint64_t id) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
		if(pendingTransactions.find(id) != pendingTransactions.end()) {
			PendingTransaction& inaction = pendingTransactions[id];
			//Begin flushing the transaction to disk
			JournalEntry entry;
			dev->Read(inaction.startloc,entry);
			entry.written = true;
			//TODO: Flush here
			dev->Write(inaction.startloc,entry);
			uint64_t id = entry.transactionId;
			uint64_t position = inaction.startloc+sizeof(entry);
			while(entry.transactionId == id) {
				//Start writing out the transaction and removing the pending change bits
				unsigned char* izard = new unsigned char[entry.size*header.blockSz];
				dev->Read(position,izard,entry.size*header.blockSz);
				dev->Write(entry.location*header.blockSz,izard,entry.size*header.blockSz);
				delete[] izard;
			}
            pendingTransactions.erase(pendingTransactions.find(id));
		}
        if(pendingTransactions.size() == 0) {
            //Clear dirty bit
            header.dirtyBit = false;
            dev->Write(0, header);
        }
	}

	void LoadBlock(uint64_t blockID, unsigned char* output, size_t offset,  size_t count) {
		std::lock_guard<std::recursive_mutex> lock(mtx);
		uint64_t position = sizeof(header)+(blockID*header.blockSz);
		if(modified.find(blockID) != modified.end()) {
			position = modified[blockID];
		}
		if((count != header.blockSz) || offset) {
			unsigned char* buffer = new unsigned char[header.blockSz];
			dev->Read(position,buffer,header.blockSz);
			memcpy(output,buffer+offset,count);
			delete[] buffer;
		}else {
			dev->Read(position,output,count);
		}

	}
	void StoreBlock(uint64_t blockID, const unsigned char* input, size_t offset,  size_t count) {
		std::lock_guard<std::recursive_mutex> lock(mtx);
		uint64_t position = sizeof(header)+(blockID*header.blockSz);
        if (position>1024*1024*800L) {
            //Tried to store beyond development quota.
            throw "down";
        }
		if(header.journal) {
			//Update this block inside of our current transaction
            if(modified.find(blockID) != modified.end()) {
             //We can optimize out the load/update procedure, since we are in the same transaction
                position = modified[blockID];
            }else {
                //Update the pending transaction and load the block from the original position into the journal
                //TODO: Is there a way to SAFELY optimize out the copy procedure in the event that count == header.blockSz?
            PendingTransaction& inaction = pendingTransactions[currentTransaction];
            modified[blockID] = inaction.startloc+inaction.length;
                unsigned char* prevblock = new unsigned char[header.blockSz];
                dev->Read(position, prevblock, header.blockSz);
                dev->Write(inaction.startloc+inaction.length, prevblock, header.blockSz);
                delete[] prevblock;
            position = inaction.startloc+inaction.length;
            inaction.length+=header.blockSz;
            }
		}
		if((count != header.blockSz) || offset) {
		unsigned char* buffer = new unsigned char[header.blockSz];
				dev->Read(position,buffer,header.blockSz);
				memcpy(buffer+offset,input,count);
				dev->Write(position,buffer,header.blockSz);
				delete[] buffer;
		}else {
			dev->Write(position,input,count);
		}
	}

	int Read(uint64_t position, void* _buffer, int count) {
		unsigned char* buffer = (unsigned char*)_buffer;
		int origcount = count;
		while(count>0) {
			uint64_t blockID = position/header.blockSz;
			size_t blockOffset = position-(blockID*header.blockSz);
			uint64_t avail = std::min((uint64_t)count,header.blockSz-blockOffset);
			LoadBlock(blockID,buffer,blockOffset,avail);
			count-=avail;
			position+=avail;
			buffer+=avail;
		}
		return origcount;
	}
	    void Write(uint64_t position, const void* _data, int count) {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            
            if(header.journal) {
                //Start transaction
                if(pendingTransactions.find(currentTransaction) == pendingTransactions.end()) {
                    //In Soviet America; Inaction is ALWAYS the best action!
                    PendingTransaction inaction;
                    inaction.startloc = header.journal+transactionSize;
                    inaction.length = 0;
                    pendingTransactions[currentTransaction] = inaction;
                }
	    	PendingTransaction& action = pendingTransactions[currentTransaction];
	    	//Write initial header];
	    	JournalEntry entry;
            //Starting block offset
            entry.location = position/header.blockSz;
            entry.written = false;
            entry.transactionId = currentTransaction;
            //TODO: DOES THIS WORK?????
            entry.size = ceildiv((uint64_t)count, header.blockSz);
            dev->Write(action.startloc+action.length, entry);
            action.length+=sizeof(JournalEntry);
            }
	    	const unsigned char* data = (const unsigned char*)_data;
	    	int origcount = count;

	    			while(count>0) {
	    				uint64_t blockID = position/header.blockSz;
	    				size_t blockOffset = position-(blockID*header.blockSz);
	    				if(blockOffset>header.blockSz) {
	    					throw "down";
	    				}
	    				uint64_t avail = std::min((uint64_t)count,header.blockSz-blockOffset);
	    				StoreBlock(blockID,data,blockOffset,avail);
	    				count-=avail;
	    				position+=avail;
	    				data+=avail;
	    			}
	    }

	    uint64_t GetLength() {
	    	return dev->GetLength();
	    }
	    ~JournaledIoStream(){

	    };
	    /*
	    void Truncate(int64_t sz) {

	    };*/
};


















class DiskRoot {
public:
    uint64_t rootdb;
    uint64_t journal;
    uint64_t currentInode;
    DiskRoot() {
        rootdb = 0;
        journal = 0;
        currentInode = 0;
    }
    
};
class ManagedDiskRoot {
public:
    OpenNet::Stream* dbstream;
    OpenNet::Stream* journalstream;
};
class Inode {
public:
    //The inode reference count (number of references to this entry)
    uint64_t key;
    int32_t refcount;
    int32_t owner;
    int32_t group;
    int32_t permissions;
    uint64_t size;
    time_t accessTime;
    time_t creationTime;
    time_t modificationTime;
    //Pointer to the first block of data
    uint64_t blockptr;
    bool operator<(const Inode& other) const {
        return key<other.key;
    }
    bool operator==(const Inode& other) const {
        return key == other.key;
    }
};
//CREATE TABLE IF NOT EXISTS Inodes (key INTEGER NOT NULL PRIMARY KEY, refcount INTEGER, owner INTEGER, ogroup INTEGER, permissions INTEGER, size INTEGER, accessTime DATETIME, creationTime DATETIME, modificationTime DATETIME, blockptr INTEGER)

//CREATE TABLE IF NOT EXISTS Files (filename varchar(256) NOT NULL PRIMARY KEY, parent INTEGER, inode INTEGER, type INTEGER)

class StreamAdapter:public MemoryAbstraction::Stream {
public:
    ::Stream val;
    StreamAdapter(const ::Stream& val) {
        this->val = val;
    }
    int Read(uint64_t position, void* buffer, int count) {
        
        int rval = val.Read(val.thisptr,position,(unsigned char*)buffer,count);
        if(rval <count) {
            //Zero-fill
            size_t mc = count-rval;
            memset(((unsigned char*)buffer)+rval,0,mc);
        }
        return count;
    }
    void Write(uint64_t position, const void* buffer, int count) {
        val.Write(val.thisptr,position,(unsigned char*)buffer,count);
    }
    virtual uint64_t GetLength() {
        return val.QueryLength(val.thisptr);
    }
    ~StreamAdapter() {
        val.Dispose(val.thisptr);
    }
};
class InternalStream:public OpenNet::Stream {
public:
    virtual void Delete() = 0;
    virtual ~InternalStream() {}
};


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
}
static int getfullname(sqlite3_vfs*, const char *zName, int nOut, char *zOut) {
    memcpy(zOut, zName, 257);
    return 0;
}
static int dorand(sqlite3_vfs* v, int nByte, char* prand) {
    for (int i = 0; i < nByte; i++) {
        prand[i] = rand();
    }
    return 0;
}
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
    OpenNet::Stream* mstream = (OpenNet::Stream*)vfile->fileHandle;
    *sz = mstream->GetLength();
    return 0;
}
static int dbwrite(sqlite3_file* fptr, const void* buffer, int iAmt, sqlite3_int64 iOfst) {
    
    VFSFile* vfile = (VFSFile*)fptr->pMethods;
    OpenNet::Stream* mstream = (OpenNet::Stream*)vfile->fileHandle;
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
    OpenNet::Stream* mstream = (OpenNet::Stream*)vfile->fileHandle;
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
    OpenNet::Stream* mstream = (OpenNet::Stream*)vfile->fileHandle;
   // printf("WARNING: TRUNCATE IS NOT IMPLEMENTED. THIS MAY CAUSE FATAL ERRORS.");
    mstream->Truncate(sz);
    return 0;
}



class IndexEntry {
public:
    //The name of the current file
    char filename[256];
    //The parent index entry (folder)
    uint64_t parentEntry;
    //The inode pointer
    uint64_t inode;
    //The type of this file (0 for directory, 1 for file, 2 for hardlink)
    int32_t filetype;
    IndexEntry() {
        memset(filename,0,sizeof(filename));
        parentEntry = 0;
        inode = 0;
        filetype = 0;
    }
};

//CREATE TABLE IF NOT EXISTS Files (filename varchar(256) NOT NULL, parent INTEGER, inode INTEGER, type INTEGER, PRIMARY KEY(filename, parent))
class DirectoryIndex:public IDisposable {
public:
    sqlite3* db;
    sqlite3_stmt* insertcommand;
    sqlite3_stmt* findcommand;
    sqlite3_stmt* traversecommand;
    sqlite3_stmt* deletecommand;
    DirectoryIndex(sqlite3* db) {
        this->db = db;
        
        std::string sql = "INSERT INTO Files VALUES (?, ?, ?, ?)";
        const char* parsed;
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &insertcommand, &parsed);
        const char* msg = sqlite3_errmsg(db);
        sql = "SELECT * FROM Files WHERE filename = ? AND parent = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(),&findcommand, &parsed);
        sql = "SELECT * FROM Files WHERE parent = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(),&traversecommand, &parsed);
        sql = "DELETE FROM Files WHERE filename = ? AND parent = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(),&deletecommand, &parsed);
        
        
    }
    void Delete(const IndexEntry& entry) {
        sqlite3_bind_text(deletecommand, 1, entry.filename, (int)strlen(entry.filename), 0);
        sqlite3_bind_int64(deletecommand, 2, entry.parentEntry);
        int val;
        while ((val = sqlite3_step(deletecommand)) !=SQLITE_DONE) {}
        sqlite3_reset(deletecommand);
    }
    template<typename F>
    void Traverse(const F& callback, uint64_t parentDir) {
        int val;
        sqlite3_bind_int64(traversecommand, 1, parentDir);
        while ((val = sqlite3_step(traversecommand)) != SQLITE_DONE) {
            if(val == SQLITE_ROW) {
                IndexEntry entry;
                const char* mptr = (const char*)sqlite3_column_text(traversecommand, 0);
                memcpy(entry.filename, mptr, sqlite3_column_bytes(traversecommand, 0));
                entry.parentEntry = sqlite3_column_int64(traversecommand, 1);
                entry.inode = sqlite3_column_int64(traversecommand, 2);
                entry.filetype = sqlite3_column_int(traversecommand, 3);
                callback(entry);
                
            }
        }
        sqlite3_reset(traversecommand);
        
        
    }
    void Insert(const IndexEntry& entry) {
        sqlite3_bind_text(insertcommand, 1, entry.filename, (int)strlen(entry.filename), 0);
        sqlite3_bind_int64(insertcommand, 2, entry.parentEntry);
        sqlite3_bind_int64(insertcommand, 3, entry.inode);
        sqlite3_bind_int(insertcommand, 4, entry.filetype);
        int val;
        while((val = sqlite3_step(insertcommand)) != SQLITE_DONE) {
           //TODO: Failing UNIQUE constraint here for some reason (duplicate filename)
        }
        sqlite3_reset(insertcommand);
    }
    bool Find(IndexEntry& entry) {
        sqlite3_bind_text(findcommand, 1, entry.filename, (int)strlen(entry.filename), 0);
        sqlite3_bind_int64(findcommand, 2, entry.parentEntry);
        int val;
        bool found = false;
        while((val = sqlite3_step(findcommand)) != SQLITE_DONE) {
            if(val == SQLITE_ROW) {
                //We have a ROW!
                found = true;
                entry.parentEntry = sqlite3_column_int(findcommand, 1);
                entry.inode = sqlite3_column_int64(findcommand, 2);
                entry.filetype = sqlite3_column_int(findcommand, 3);
                break;
            }
        }
        sqlite3_reset(findcommand);
        return found;
    }
};


class InodeIndex {
public:
    sqlite3* db;
    sqlite3_stmt* insertcommand;
    sqlite3_stmt* findcommand;
    sqlite3_stmt* updatecommand;
    sqlite3_stmt* deletecommand;
    InodeIndex(sqlite3* db) {
        this->db = db;
        const char* parsed;
        std::string sql = "INSERT INTO Inodes VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &insertcommand, &parsed);
        const char* err = sqlite3_errmsg(db);
        sql = "SELECT * FROM Inodes WHERE key = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &findcommand, &parsed);
        sql = "UPDATE Inodes SET refcount = ?, owner = ?, ogroup = ?, permissions = ?, size = ?, accessTime = ?, creationTime = ?, modificationTime = ?, blockptr = ? WHERE key = ?";
        
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &updatecommand, &parsed);
        sql = "DELETE FROM Inodes WHERE key = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &deletecommand, &parsed);
        
    }
    void Delete(const Inode& node) {
        sqlite3_bind_int64(deletecommand, 1, node.key);
        while(sqlite3_step(deletecommand) != SQLITE_DONE){}
        sqlite3_reset(deletecommand);
    }
    void Update(const Inode& node) {
        sqlite3_bind_int(updatecommand, 1, node.refcount);
        sqlite3_bind_int(updatecommand, 2, node.owner);
        sqlite3_bind_int(updatecommand, 3, node.group);
        sqlite3_bind_int(updatecommand, 4, node.permissions);
        sqlite3_bind_int64(updatecommand, 5, node.size);
        sqlite3_bind_int64(updatecommand, 6, node.accessTime);
        sqlite3_bind_int64(updatecommand, 7, node.creationTime);
        sqlite3_bind_int64(updatecommand, 8, node.modificationTime);
        sqlite3_bind_int64(updatecommand, 9, node.blockptr);
        sqlite3_bind_int64(updatecommand, 10, node.key);
        int val;
        while((val = sqlite3_step(updatecommand)) != SQLITE_DONE){}
        sqlite3_reset(updatecommand);
    }
    void Insert(const Inode& node) {
        sqlite3_bind_int64(insertcommand, 1, node.key);
        sqlite3_bind_int(insertcommand, 2, node.refcount);
        sqlite3_bind_int(insertcommand, 3, node.owner);
        sqlite3_bind_int(insertcommand, 4, node.group);
        sqlite3_bind_int(insertcommand, 5, node.permissions);
        sqlite3_bind_int64(insertcommand, 6, node.size);
        sqlite3_bind_int64(insertcommand, 7, node.accessTime);
        sqlite3_bind_int64(insertcommand, 8, node.creationTime);
        sqlite3_bind_int64(insertcommand, 9, node.modificationTime);
        sqlite3_bind_int64(insertcommand, 10, node.blockptr);
        int val;
        while ((val = sqlite3_step(insertcommand)) != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(db);
            std::cout<<err<<std::endl;
        }
        sqlite3_reset(insertcommand);
    }
    
    //CREATE TABLE IF NOT EXISTS Inodes (key INTEGER NOT NULL PRIMARY KEY, refcount INTEGER, owner INTEGER, group INTEGER, permissions INTEGER, size INTEGER, accessTime DATETIME, creationTime DATETIME, modificationTime DATETIME, blockptr INTEGER)
    bool Find(Inode& node) {
        sqlite3_bind_int64(findcommand, 1, node.key);
        bool found = false;
        int val;
        
        while ((val = sqlite3_step(findcommand)) != SQLITE_DONE) {
            if(val == SQLITE_ROW) {
                found = true;
                node.refcount = sqlite3_column_int(findcommand, 1);
                node.owner = sqlite3_column_int(findcommand, 2);
                node.group = sqlite3_column_int(findcommand, 3);
                node.permissions = sqlite3_column_int(findcommand, 4);
                node.size = sqlite3_column_int64(findcommand, 5);
                node.accessTime = sqlite3_column_int64(findcommand, 6);
                node.creationTime = sqlite3_column_int64(findcommand, 7);
                node.modificationTime = sqlite3_column_int64(findcommand, 8);
                node.blockptr = sqlite3_column_int64(findcommand, 9);
                break;
            }
        }
        sqlite3_reset(findcommand);
        return found;
    }
};
static int dbdelete(sqlite3_vfs* fptr, const char *zName, int syncDir) {
    // printf("WARNING: TRUNCATE (delete) IS CURRENTLY A NO-OP\n");
    ManagedDiskRoot* realfs = (ManagedDiskRoot*)fptr->pAppData;
    std::string ms = zName;
    if(ms == "root.db") {
        realfs->dbstream->Truncate(0);
    }else {
        realfs->journalstream->Truncate(0);
    }
    return 0;
}
static int dbopen(sqlite3_vfs* fs, const char *zName, sqlite3_file* mfile, int flags, int *pOutFlags) {
    ManagedDiskRoot* realfs = (ManagedDiskRoot*)fs->pAppData;
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
                    realptr->fileHandle = realfs->dbstream;
                }
                else {
                    realptr->fileHandle = realfs->journalstream;
                }
    return 0;
}
//Represents a filesystem extent
class Extent {
public:
    uint64_t size;
    uint64_t ptr;
    //External bit -- used when this block does not point to data, but instead --
    //points to a continuation of the extent table. This will usually be the very last
    //extent in the table.
    bool external;
    Extent() {
        external = false;
        ptr = 0;
        size = 0;
    }
};
class ExtentTable {
public:
    //The length of the file
    uint64_t len;
    //Originally 420, but value caused fragmentation. Increased to 1024.
    Extent table[1024];
    ExtentTable() {
        memset(table, 0, sizeof(table));
        len = 0;
    }
};
static OpenNet::Stream* OpenStream(void* fs,uint64_t start);
class DiskFS:public OpenNet::IFilesystem {
public:
    std::recursive_mutex mtx;
    MemoryAbstraction::Stream* mstr;
    JournaledIoStream* journalController;
    MemoryAbstraction::MemoryAllocator* alloc;
    MemoryAbstraction::Reference<DiskRoot> rootPtr;
    std::map<uint64_t,OpenNet::Stream*> streams;
    ManagedDiskRoot root;
    sqlite3* db;
    //The file index
    DirectoryIndex* index;
    //The inode index
    InodeIndex* inodes;
    ~DiskFS() {
        delete index;
        delete inodes;
    }
    //Resolves a directory to its corresponding parent INODE pointer. The root INODE is always 0.
    uint64_t ResolveDirectory(const std::string& path, std::string& filename, uint64_t& inode) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        
       uint64_t currentDirectory = 0;
        std::vector<std::string> components = GetFileComponents(path);
        IndexEntry entry;
        Inode node;
        node.key = 0;
        inode = 0;
        for(int i = 0;i<components.size();i++) {
            memset(entry.filename,0,sizeof(entry.filename));
            entry.parentEntry = node.key;
            memcpy(entry.filename,components[i].data(),components[i].size());
            if(!index->Find(entry)) {
                return -1;
            }
            //We have hit our node on the head!
            if(entry.filename == components.back() && i == components.size()-1) {
                filename = components[i];
                inode = entry.inode;
                return currentDirectory;
            }
            node.key = entry.inode;
            inodes->Find(node);
            currentDirectory = node.key;
        }
        return currentDirectory;
    }
    void EnumerateNodes(const std::string& path, std::function<bool(const FileInformation&)> callback) {
        //For now; assume root path
        std::lock_guard<std::recursive_mutex> lock(mtx);
        
        std::string fname;
        uint64_t inode;
        uint64_t dir = ResolveDirectory(path,fname,inode);
        
        index->Traverse([=](const IndexEntry& val){
            //Resolve the inode
            Inode node;
            node.key = val.inode;
            inodes->Find(node);
            FileInformation info;
            info.created = node.creationTime;
            info.isFile = val.filetype;
            info.permissions = node.permissions;
            info.owner = node.owner;
            info.group = node.group;
            info.lastModified = node.modificationTime;
            info.size = node.size;
            info.inode = node.key;
            memcpy(info.name,val.filename,sizeof(val.filename));
            return callback(info);
        },inode);
    }
    bool Exists(const std::string& filename) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        std::string fname;
        uint64_t inode;
       uint64_t dir = ResolveDirectory(filename,fname,inode);
        if(dir == -1) {
            return false;
        }
        return true;
    }
    std::string mcb(const std::string& r) {
        if(r.size()) {
            return r.substr(1,std::string::npos);
        }
        return r;
    }
    void Rename(const std::string& oldname, const std::string& newname) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        std::string fname;
        uint64_t inode;
        uint64_t dir = ResolveDirectory(oldname,fname,inode);
        IndexEntry entry;
        entry.parentEntry = dir;
        fname = GetFileComponents(oldname).back();
        memcpy(entry.filename,fname.data(),fname.size());
        index->Find(entry);
        index->Delete(entry);
        //Insert into new directory
        uint64_t oldDir = dir;
        //TODO: PROBLEM HERE. When the ENTRY is a DIRECTORY, it's PARENT returns ITSELF.
        std::vector<std::string> victor = GetFileComponents(newname);
        victor.pop_back();
        dir = ResolveDirectory(mcb(JoinFileComponents(victor,"","/")),fname,inode);
        if(dir == -1) {
            dir = oldDir;
        }
        if(inode) {
            dir = inode;
        }
        memset(entry.filename,0,sizeof(entry.filename));
        entry.parentEntry = dir;
        std::string safename = GetFileComponents(newname).back();
        memcpy(entry.filename,safename.data(),safename.size());
        index->Insert(entry);
        
    }
    //WARNING: NOT ABI-SAFE!
    OpenNet::Stream* GetStream(const std::string& filename) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        std::string fname;
        uint64_t inode;
       uint64_t dir = ResolveDirectory(filename,fname,inode);
        Inode node;
        node.key = inode;
        inodes->Find(node);
        return OpenStream(this,node.blockptr);
    }
    void UpdateMetadata(const FileInformation& info) {
        
        std::lock_guard<std::recursive_mutex> lock(mtx);
        Inode node;
        node.key = info.inode;
        inodes->Find(node);
        node.owner = info.owner;
        node.group = info.group;
        node.permissions = info.permissions;
        node.modificationTime = info.lastModified;
        node.creationTime = info.created;
        inodes->Update(node);
    }
    void Stat(const std::string& _filename, FileInformation& info) {
        
        std::lock_guard<std::recursive_mutex> lock(mtx);
        std::string fname;
        uint64_t inode;
        uint64_t dir = ResolveDirectory(_filename,fname,inode);
        IndexEntry entry;
        Inode node;
        entry.parentEntry = dir;
        memcpy(entry.filename,fname.data(),fname.size());
        index->Find(entry);
        node.key = entry.inode;
        inodes->Find(node);
        info.created = node.creationTime;
        info.isFile = entry.filetype;
        info.lastModified = node.modificationTime;
        info.owner = node.owner;
        info.group = node.group;
        info.permissions = node.permissions;
        info.inode = node.key;
        if(node.blockptr != 0) {
        auto bot = OpenStream(this, node.blockptr);
        info.size = bot->GetLength();
        delete bot;
        }
        memcpy(info.name,entry.filename,sizeof(entry.filename));
        
    }
    sqlite3_stmt* _deletexattr_all;
    sqlite3_stmt* _setxattr;
    sqlite3_stmt* _newxattr;
    sqlite3_stmt* _getxattr;
    sqlite3_stmt* _listxattr;
    sqlite3_stmt* _removexattr;
    bool removexattr(const FileInformation& node, const char* key) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        sqlite3_bind_int64(_removexattr, 1, node.inode);
        sqlite3_bind_text(_removexattr, 2, key, (int)strlen(key), 0);
        while (sqlite3_step(_removexattr) != SQLITE_DONE) {
            
        }
        sqlite3_reset(_removexattr);
        return sqlite3_changes(db);
    }
    size_t getxattr(const FileInformation& node, const char* key, void* output, size_t bufferSize) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        sqlite3_bind_int64(_getxattr, 1, node.inode);
        sqlite3_bind_text(_getxattr, 2, key, (int)strlen(key), 0);
        int val;
        size_t found = 0;
        while((val = sqlite3_step(_getxattr)) != SQLITE_DONE) {
            if (val == SQLITE_ROW) {
                
                const void* data = sqlite3_column_blob(_getxattr,2);
                size_t avail = ((size_t)sqlite3_column_bytes(_getxattr, 2));
                if(avail<=bufferSize) {
                memcpy(output, data, avail);
                }
                    found = avail;
                
                break;
            }
        }
        sqlite3_reset(_getxattr);
        return found;
    }
    size_t listxattr(const FileInformation& node, char* list, size_t len) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        sqlite3_bind_int64(_listxattr, 1, node.inode);
        size_t olen = len;
        size_t retval = 0;
        int val;
        while ((val = sqlite3_step(_listxattr)) != SQLITE_DONE) {
            if (val == SQLITE_ROW) {
                const char* dptr = (const char*)sqlite3_column_text(_listxattr, 1);
                size_t slen = strlen(dptr)+1;
                
                if(list == 0) {
                    retval+=slen;
                }else {
                if (slen<=len) {
                    memcpy(list, dptr, slen);
                    list+=slen;
                    len-=slen;
                    retval+=slen;
                }
                }
                
            }
        }
        sqlite3_reset(_listxattr);
        return retval;
    }
    void setxattr(const FileInformation& node, const char* key, const char* value, size_t size) {
        
        std::lock_guard<std::recursive_mutex> lock(mtx);
        
        sqlite3_bind_int64(_getxattr, 1, node.inode);
        sqlite3_bind_text(_getxattr, 2, key, (int)strlen(key), 0);
        int val;
        bool found = false;
        while ((val = sqlite3_step(_getxattr)) !=SQLITE_DONE) {
            if(val == SQLITE_ROW) {
                found = true;
                break;
            }
        }
        sqlite3_reset(_getxattr);
        
        
        

        if(found) {
            sqlite3_bind_text(_setxattr, 1, value, (int)size, 0);
            sqlite3_bind_int64(_setxattr, 2, node.inode);
            sqlite3_bind_text(_setxattr, 3, key, (int)strlen(key), 0);
            
            while (sqlite3_step(_setxattr) != SQLITE_DONE) {}
            sqlite3_reset(_setxattr);
        }else {
            sqlite3_bind_int64(_newxattr, 1, node.inode);
            sqlite3_bind_text(_newxattr, 2, key, (int)strlen(key), 0);
            sqlite3_bind_text(_newxattr, 3, value, (int)size, 0);
            while (sqlite3_step(_newxattr) != SQLITE_DONE) {}
            sqlite3_reset(_newxattr);
        }
        
    }
    
    
    void Delete(const std::string& path) {
        
        std::lock_guard<std::recursive_mutex> lock(mtx);
        std::string fname;
        uint64_t inode;
        uint64_t dir = ResolveDirectory(path,fname,inode);
        if(fname.size() == 0) {
            //Directory
            //TODO: Not yet implemented
            
        }else {
            //File
            IndexEntry entry;
            entry.parentEntry = dir;
            memcpy(entry.filename,fname.data(),fname.size());
            index->Find(entry);
            Inode node;
            node.key = entry.inode;
            inodes->Find(node);
            if(node.refcount !=0) {
                node.refcount--;
            }
            inodes->Update(node);
            if(node.refcount <= 0 && node.blockptr) {
                //De-allocate physical storage
                InternalStream* s = (InternalStream*)OpenStream(this,node.blockptr);
                s->Delete();
                delete s;
                
                //Delete extended attributes
                sqlite3_bind_int64(_deletexattr_all, 1, node.key);
                while (sqlite3_step(_deletexattr_all) != SQLITE_DONE) {
                    
                }
                sqlite3_reset(_deletexattr_all);
                //Delete node
                
                inodes->Delete(node);
            }
            //Delete file entry
            index->Delete(entry);
        }
    }
    void Mknod(const std::string& path, bool isFile) {
        
        std::lock_guard<std::recursive_mutex> lock(mtx);
        //Create file (for now; assume root directory)
        std::string fname;
        uint64_t inode;
        std::vector<std::string> components = GetFileComponents(path);
        std::string realname = components.back();
        components.pop_back();
        std::string pathd = JoinFileComponents(components,"","/");
        if(pathd.size()) {
            pathd = pathd.substr(1,std::string::npos);
        }
        uint64_t dir = ResolveDirectory(pathd,fname,inode);
        dir = inode;
        
        IndexEntry entry;
        memcpy(entry.filename,realname.data(),realname.size());
        Inode node;
        time_t t;
        time(&t);
        node.accessTime = t;
        node.creationTime = t;
        node.modificationTime = t;
        node.owner = 0;
        node.group = 0;
        node.refcount = 1;
        node.size = 0;
        node.blockptr = 0;
        if (isFile) {
            node.blockptr = alloc->Allocate<ExtentTable>().offset;
        }
        DiskRoot root = rootPtr;
        node.key = root.currentInode;
        root.currentInode++;
        entry.filetype = isFile;
        entry.inode = node.key;
        //Root directory
        entry.parentEntry = dir;
        //Update data structures on disk
        index->Insert(entry);
        inodes->Insert(node);
        rootPtr = root;
        
    }
    DiskFS() {
        
        size_t sz = 1024*1024*1024;
        char* mander = new char[1024*1024];
        struct stat ms;
        memset(mander,0,1024*1024);
        if(stat("fs",&ms)) {
            
            FILE* mptr = fopen("fs","wb");
            /*for(uint64_t i = 0;i<sz;i+=1024*1024) {
                fwrite(mander,1,1024*1024,mptr);
            }*/
            fclose(mptr);
        }
        delete[] mander;
        //int fd = open("fs",O_RDWR);
        //mstr = new MemoryAbstraction::RegularFileStream(fopen("fs","r+b"));
        journalController = new JournaledIoStream(new MemoryAbstraction::RegularFileStream(fopen("fs", "r+b")),1024*512,(uint64_t)-1);
        mstr = journalController;
        
        uint64_t rootptr = 0;
        //1TB max block size
        alloc = new MemoryAbstraction::MemoryAllocator(mstr,rootptr,-1);
        if(rootptr == 0) {
            rootPtr = alloc->Allocate<DiskRoot>();
            rootptr = rootPtr.offset;
            
            //COMMTENTED OUT FOR DEBUGGING
            //printf("TODO: SET THE ROOT POINTER BY UNCOMMENTING THIS LINE!!!!!!!!!!!!!!!!!!!!!\n");
            alloc->SetRootPtr(rootPtr);
        }else {
            rootPtr = MemoryAbstraction::Reference<DiskRoot>(alloc->str,rootptr);
        }
        DiskRoot root = rootPtr;
        if(root.rootdb == 0) {
            root.rootdb = alloc->Allocate<ExtentTable>().offset;
            root.journal = alloc->Allocate<ExtentTable>().offset;
            this->rootPtr = root;
        }
        //Initialize volume journal
        if(journalController->header.journal == 0) {
        	uint64_t jsize = 1024*1024*500;
            //TODO: THE JOURNAL IS CAUSING WRITES TO NOT GO THROUGH PROPERLY
            //THIS IS CAUSING THE PROBLEM; NOT THE ALLOCATION OF THE JOURNAL. FIX ASAP.
            
            journalController->SetJournal(alloc->Allocate(jsize),jsize);
        }
       uint64_t transaction = journalController->BeginTransaction();



        this->root.journalstream = OpenStream(this, root.journal);
        this->root.dbstream = OpenStream(this, root.rootdb);
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
        vfs.pAppData = &this->root;
        sqlite3_vfs_register(mptr, 1);
        
        sqlite3_open("root.db", &db);
        char* err;
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS Files (filename varchar(256) NOT NULL, parent INTEGER, inode INTEGER, type INTEGER, PRIMARY KEY(filename, parent))", 0, 0, &err);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS Inodes (key INTEGER NOT NULL PRIMARY KEY, refcount INTEGER, owner INTEGER, ogroup INTEGER, permissions INTEGER, size INTEGER, accessTime DATETIME, creationTime DATETIME, modificationTime DATETIME, blockptr INTEGER)", 0, 0, &err);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS ExtendedAttributes (inode INTEGER NOT NULL, key varchar(256), value BLOB)", 0, 0, &err);
        sqlite3_exec(db, "CREATE INDEX AttrLookupIdx ON ExtendedAttributes (inode)", 0, 0, &err);
        std::string sql = "DELETE FROM ExtendedAttributes WHERE inode = ?";
        const char* parsed;
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_deletexattr_all, &parsed);
        sql = "UPDATE ExtendedAttributes SET value = ? WHERE inode = ? AND key = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_setxattr, &parsed);
        sql = "INSERT INTO ExtendedAttributes VALUES (?, ?, ?)";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_newxattr, &parsed);
        sql = "SELECT * FROM ExtendedAttributes WHERE inode = ? AND key = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_getxattr, &parsed);
        const char* errmsg = sqlite3_errmsg(db);
        sql = "SELECT * FROM ExtendedAttributes WHERE inode = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_listxattr, &parsed);
        sql = "DELETE FROM ExtendedAttributes WHERE inode = ? AND key = ?";
        sqlite3_prepare(db, sql.data(), (int)sql.size(), &_removexattr, &parsed);
        index = new DirectoryIndex(db);
        inodes = new InodeIndex(db);
        journalController->EndTransaction(transaction);
    }
};

const size_t TABLESZ = 1024;
class FileStream:public InternalStream {
public:
    //The root node (where the extent table starts)
    uint64_t start;
    //Pointer to underlying filesystem
    DiskFS* fs;
    ExtentTable table;
    uint32_t refcount;
    uint64_t TableSeek(uint64_t position, int& ext) {
        uint64_t cpos = 0;
        int& i = ext;
        i = 0;
        while(cpos <=position) {
            if(table.table[i].ptr == 0) {
                return 0;
            }
            cpos+=table.table[i].size;
            i++;
        }
        i--;
        
        return cpos;
    }
    void Truncate(int64_t size) {
         std::lock_guard<std::recursive_mutex> lock(fs->mtx);
        if(table.len == size) {
            return;
        }
        if(size<table.len) {
            //TRIM segments
            size_t total = 0;
            size_t lastChunk = 0;
            for(size_t i = 0;i<TABLESZ;i++) {
                if(table.table[i].ptr == 0) {
                    break;
                }
                total+=table.table[i].size;
                lastChunk = i;
            }
            for(size_t i = lastChunk;i != -1;i--) {
                if(total>size) {
                    total-=table.table[i].size;
                }
                if(total != size) {
                    //We can't remove the last block; or we would fragment! We'll just update the size metadata
                    //without actually changing chunk size here. That will avoid fragmentation at the cost of slightly increased
                    //disk space usage.
                    break;
                }else {
                    //Just de-allocate the block!
                    fs->alloc->Free(table.table[i].ptr, table.table[i].size);
                    table.table[i].ptr = 0;
                    table.table[i].size = 0;
                }
            }
        }else {
            //Not yet implemented
            Allocate(size);
        }
        table.len = size;
        fs->alloc->str->Write(start, table);
    }
    void Delete() {
        for(size_t i = 0;table.table[i].ptr;i++) {
            if(table.table[i].external) {
                //TODO: Not yet implemented
                throw "up";
            }
            fs->alloc->Free(table.table[i].ptr,table.table[i].size);
        }
    }
    int Read(int64_t position, unsigned char* buffer, int count) {
         std::lock_guard<std::recursive_mutex> lock(fs->mtx);
        int available = (int)std::min(table.len-position,(uint64_t)count);
        count = available;
        while(count>0) {
            //Seek to intended position
            int extent;
            uint64_t sval = position;
            int blockOffset;
            int blockAvail;
            for(extent = 0;;extent++) {
                if(table.table[extent].size>sval) {
                    //We've found an extent that matches the position (maybe)!
                    blockOffset = sval;
                    blockAvail = table.table[extent].size-blockOffset;
                    if(blockAvail == 0) {
                        //WUT?!?!?!
                        throw "up";
                    }
                    break;
                }
                sval-=table.table[extent].size;
            }
            //Available data in block
            blockAvail = std::min(blockAvail,count);

            //Read the data into base and increment pointers
            fs->alloc->str->Read(table.table[extent].ptr+blockOffset,buffer,blockAvail);
            if(blockAvail>count) {
                        	throw "down";
                        }
            position+=blockAvail;
            buffer+=blockAvail;
            count-=blockAvail;
        }
        return available;
    }
    void Allocate(uint64_t size) {
         std::lock_guard<std::recursive_mutex> lock(fs->mtx);
        if(size>table.len) {
            //Add new extent
            int blocksz = DISTFS_BLOCK_SIZE;
            size_t i;
            uint64_t realsize = 0;
            for(i = 0;;i++) {
                if(table.table[i].ptr == 0) {
                    break;
                }
                realsize+=table.table[i].size;
            }
            if(size<realsize) {
                
            }else {
                int newblocks = (int)std::ceil(((double)((size)-realsize))/blocksz)+1;
                uint64_t allocsz = newblocks*blocksz;
                table.table[i].ptr = fs->alloc->Allocate(allocsz);
                table.table[i].size = allocsz;
            }
            table.len = size;
            fs->alloc->str->Write(start,table);
        }
    }
    void Write(int64_t position, unsigned char* data, int count) {
         std::lock_guard<std::recursive_mutex> lock(fs->mtx);
        Allocate(position+count);
        while(count>0) {
            //Seek to intended position
            int extent;
            uint64_t sval = position;
            int blockOffset;
            int blockAvail;
            for(extent = 0;;extent++) {
                if(table.table[extent].size>sval) {
                    //We've found an extent that matches the position (maybe)!
                    blockOffset = sval;
                    blockAvail = table.table[extent].size-blockOffset;
                    if(blockAvail == 0) {
                        //WUT?!?!?!
                        throw "up";
                    }
                    break;
                }
                sval-=table.table[extent].size;
            }
            //Available data in block
            blockAvail = std::min(blockAvail,count);
            fs->alloc->str->Write(table.table[extent].ptr+(uint64_t)blockOffset,data,blockAvail);
            position+=blockAvail;
            data+=blockAvail;
            count-=blockAvail;
        }
    }
    int64_t GetLength() {
        return table.len;
    }
    FileStream(DiskFS* fs,uint64_t start) {
        this->start = start;
        this->fs = fs;
        fs->alloc->str->Read(start,table);
        refcount = 0;
    }
    ~FileStream() {
        fs->alloc->str->Write(start, table);
        std::lock_guard<std::recursive_mutex> lock(fs->mtx);
        fs->streams.erase(fs->streams.find(start));
    }
};
class RefcountedStream:public InternalStream {
public:
    FileStream* mstr;
    RefcountedStream(FileStream* other) {
        mstr = other;
        mstr->refcount++;
    }
    void Delete() {
        mstr->Delete();
    }
    int Read(int64_t position, unsigned char* buffer, int count) {
        return mstr->Read(position, buffer, count);
    }
    void Write(int64_t position, unsigned char* data, int count) {
        mstr->Write(position, data, count);
    }
    int64_t GetLength()  {
        return mstr->GetLength();
    }
    
    void Truncate(int64_t sz) {
        mstr->Truncate(sz);
    };
    
    ~RefcountedStream() {
         std::lock_guard<std::recursive_mutex> lock(mstr->fs->mtx);
        mstr->refcount--;
        if(mstr->refcount == 0) {
            delete mstr;
        }
    }
};


static OpenNet::Stream* OpenStream(void* fs,uint64_t start) {
    
    DiskFS* realfs = (DiskFS*)fs;
    std::lock_guard<std::recursive_mutex> lock(realfs->mtx);
    if(realfs->streams.find(start) == realfs->streams.end()) {
   auto bot = new FileStream((DiskFS*)fs,start);
        
        realfs->streams[start] = bot;
        return new RefcountedStream(bot);
    }else {
        return new RefcountedStream((FileStream*)realfs->streams[start]);
    }
}
extern "C" {
    
    //Gets the underlying OS filesystem
    void OS_GetFilesystem(Filesystem* fsptr) {
        ManagedToNativeFS(new DiskFS(),fsptr);
    }
}

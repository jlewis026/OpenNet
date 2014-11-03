#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "MemoryAbstraction.h"
#include "OpenNet.h"
#include <map>
#include <stdint.h>
#include <mutex>
namespace OpenNet {


namespace Internals {
class JournaledIOStream:public MemoryAbstraction::Stream {
public:

    uint64_t cacheSz;
    MemoryAbstraction::Stream* baseDev;
    MemoryAbstraction::Stream* journal;
    ///Creates a new; safe block device. Commits will be written when Flush is called
    /// @param baseDev The underlying, raw block device that is unprotected.
    /// @param journal The journal, which can also be an unprotected block device; ideally on a separate medium (such as a tape drive; if those still exist) than the base device.
    JournaledIOStream(MemoryAbstraction::Stream* baseDev, MemoryAbstraction::Stream* journal) {
        this->baseDev = baseDev;
        this->journal = journal;
        //Cache size of 5MB by default.
        this->cacheSz = 1024*1024*5;
        this->journalPosition = 0;
        uint64_t transactionDescriptor = 0;
        journal->Read(this->journalPosition,transactionDescriptor);
        transactionDescriptor+=8;
        //IF we have a valid transaction descriptor at this address, run journal....
        unsigned char* buffer = new unsigned char[cacheSz];
        while(transactionDescriptor) {
            for(uint64_t i = 0;i<transactionDescriptor;i++) {
                //Write each transaction to disk
                uint64_t offset;
                journal->Read(journalPosition,offset);
                journalPosition+=8;
                journal->Read(journalPosition,buffer,cacheSz);
                baseDev->Write(offset,buffer,cacheSz);
                journalPosition+=cacheSz;
            }
        }
        //Journal PAYBACK complete!
        delete[] buffer;
        transactionDescriptor = 0;
        journal->Write(0,transactionDescriptor);
        journalPosition = 0;
    }
    ///A list of uncommitted, dirty blocks; whose content may frequently change.
    std::map<uint64_t,uint64_t> dirtyBlocks;
    uint64_t journalPosition;
    std::recursive_mutex mtx;
    ///Writes all changes in the journal to disk; creating a new transaction.
    /// IMPORTANT NOTE: CALLING THIS FUNCTION FREQUENTLY MAY RESULT IN SUBSTANTIAL PERFORMANCE PENALTIES.
    void Flush() {
        //Flush changes to disk
        unsigned char* buffer = new unsigned char[cacheSz];
        mtx.lock();
        uint64_t ms = dirtyBlocks.count();
        journal->Write(0,ms);
        while(dirtyBlocks.count()) {
            uint64_t loc = dirtyBlocks.begin()->first;
            uint64_t jLoc = (dirtyBlocks.begin()->second)+8;
            dirtyBlocks.erase(dirtyBlocks.begin());
            mtx.unlock();
            journal->Read(jLoc,buffer);
            baseDev->Write(loc,buffer,cacheSz);
            mtx.lock();

        }
        mtx.unlock();
        delete[] buffer;
    }
    int Read(uint64_t position, void *buffer, int count) {
        unsigned char* izard = (unsigned char*)buffer;
        while(count>0) {
            if(count>=cacheSz) {
                ReadBlock(position,izard);
                izard+=cacheSz;
                position+=cacheSz;
                count-=cacheSz;
            }else {
                unsigned char* cacheLine = new unsigned char[cacheSz];
                ReadBlock(position,cacheLine);
                memcpy(izard,cacheLine,count);
                count = 0;
                delete[] cacheLine;
            }
        }
    }
    void Write(uint64_t position, const void *buffer, int count) {
        unsigned char* mander = (unsigned char*)buffer;
        while(count>0) {
            uint64_t alignment = (position/cacheSz)*cacheSz;
            if(count>=cacheSz && position == alignment) {
                WriteBlock(position,mander);
                mander+=cacheSz;
                count-=cacheSz;
                position+=cacheSz;
            }else {
                unsigned char* block = new unsigned char[cacheSz];
                ReadBlock(alignment,block);
                size_t blockOffset = (size_t)(position-alignment);
                int blockAvail = std::min(count,cacheSz-blockOffset);
                memcpy(block+blockOffset,mander,blockAvail);
                WriteBlock(position,block);
                delete[] block;
            }
        }
    }

    void ReadBlock(uint64_t position, void *buffer) {
        mtx.lock();
        if(dirtyBlocks.find(position)) {
            uint64_t pos = dirtyBlocks[position]+8;
            journal->Read(pos,buffer,cacheSz);
            mtx.unlock();
        }else {
            baseDev->Read(position,buffer,cacheSz);
            mtx.unlock();
        }

    }

    void WriteBlock(uint64_t position, const void *buffer) {
        mtx.lock();
        //Check to see if dirty
        if(dirtyBlocks.find(position)) {
            //We are dirty, so update existing chunk in journal.
            uint64_t pos = dirtyBlocks[position]+8;
            journal->Read(pos,buffer,cacheSz);
        }else {
            //We are not dirty, create new entry in journal
            dirtyBlocks[position] = journalPosition;
            journal->Write(journalPosition,position);
            journalPosition+=8;
            journal->Write(journalPosition,buffer,cacheSz);
        }
        mtx.unlock();
    }

    uint64_t GetLength() {
        return baseDev->GetLength();
    }
};
}


namespace FileSystem {
}
}

#endif // FILESYSTEM_H

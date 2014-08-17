#include <time.h>
#include <vector>
#include <stdint.h>
#include "platform.h"
#define DISTFS_BLOCK_SIZE (1024*1024)
//#define DISTFS_BLOCK_SIZE (4096)
//Definition of a Stream -- used for I/O from files.
typedef struct {
    void* thisptr;
    int(*Read)(void* thisptr,int64_t position,unsigned char* buffer, int count);
    void(*Write)(void* thisptr,int64_t position,unsigned char* data, int count);
    int64_t(*QueryLength)(void* thisptr);
    void(*Truncate)(void* thisptr, int64_t sz);
    void(*Dispose)(void* thisptr);
} Stream;
typedef struct {
    int64_t size;
    time_t lastModified;
    time_t created;
    uint32_t permissions;
    uint32_t owner;
    uint32_t group;
    uint64_t inode;
    bool isFile;
    char name[256];
} FileInformation;
typedef struct {
    void* thisptr;
    bool(*callback)(void* thisptr, const FileInformation* file);
} EnumerationCallback;

typedef struct {
    void* thisptr;
    bool(*Exists)(void* thisptr,const char* path);
    void(*GetStream)(void* thisptr,const char* path, Stream* output);
    void(*Stat)(void* thisptr, const char* path,FileInformation* infoBuffer);
    void(*Dispose)(void* thisptr);
    void(*Mknod)(void* thisptr, const char* path, bool isFile);
    void(*EnumerateNodes)(void* thisptr, const char* path, EnumerationCallback callback);
    void(*Delete)(void* thisptr, const char* path);
    void(*Rename)(void* thisptr, const char* oldpath, const char* newpath);
    void(*UpdateMetadata)(void* thisptr, const FileInformation* info);
    bool(*removexattr)(void* thisptr, const FileInformation* info, const char* key);
    size_t(*getxattr)(void* thisptr, const FileInformation* info, const char* key, void* output, size_t bufferSize);
    void(*setxattr)(void* thisptr, const FileInformation* info, const char* key, const void* value, size_t bufferSize);
    size_t(*listxattr)(void* thisptr, const FileInformation* info, char* list, size_t len);
} Filesystem;
extern "C" {
    //These functions must be implemented by the operating system
    //Makes a GUID represented by a file name
    void OS_MkGuid(char output[256]);
    //Parses a GUID
    void OS_ParseGuid(const char* input, unsigned char output[16]);
    //Converts a GUID to a string
    void OS_GuidToString(const unsigned char input[16], char output[256]);
    //Gets the underlying OS filesystem
    void OS_GetFilesystem(Filesystem* fsptr);
    void* OS_AES_GetKeyHandle(unsigned char key[32]);
    void OS_Aes_Encrypt(void* keyHandle, unsigned char* data, size_t sz,unsigned char iv[16]);
    void OS_Aes_Decrypt(void* keyHandle, unsigned char* data, size_t sz, unsigned char iv[16]);
    //These functions must be implemented by OpenNet
    void OpenNet_MountFS(const Filesystem* mountPoint, Filesystem* mountedFS, char key[32]);
    void OpenNet_Free(void* ptr);
}

#ifdef __cplusplus
#include <functional>
#include <memory>
namespace OpenNet {
    
    class IDisposable {
    public:
        virtual ~IDisposable(){};
    };
    class Stream:public IDisposable {
    public:
        virtual int Read(int64_t position, unsigned char* buffer, int count) = 0;
        virtual void Write(int64_t position, unsigned char* data, int count) = 0;
        virtual int64_t GetLength() = 0;
        virtual ~Stream(){};
        
        virtual void Truncate(int64_t sz) {};
        template<typename T>
        int Read(int64_t position, T& data) {
            return Read(position, (unsigned char*)&data, sizeof(data));
        }
        template<typename T>
        void Write(int64_t position, const T& data) {
            Write(position, (unsigned char*)&data, sizeof(data));
        }
        
    };
    class ABI_Stream :public Stream {
    public:
        ::Stream underlyingStream;
        ABI_Stream(const ::Stream& str) {
            this->underlyingStream = str;
        }
        int Read(int64_t position, unsigned char* data, int count) {
            return underlyingStream.Read(underlyingStream.thisptr, position, data, count);
            
        }
        void Write(int64_t position, unsigned char* data, int count) {
            underlyingStream.Write(underlyingStream.thisptr, position, data, count);
        }
        void Truncate(int64_t size) {
            underlyingStream.Truncate(underlyingStream.thisptr,size);
        }
        int64_t GetLength() {
            return underlyingStream.QueryLength(underlyingStream.thisptr);
        }
        ~ABI_Stream() {
            underlyingStream.Dispose(underlyingStream.thisptr);
        }
    };
    static int ABI_Read(void* thisptr, int64_t position, unsigned char* data, int count) {
        Stream* obj = (Stream*)thisptr;
        return obj->Read(position, data, count);
    }
    static void ABI_Write(void* thisptr, int64_t position, unsigned char* data, int count) {
        Stream* obj = (Stream*)thisptr;
        obj->Write(position, data, count);
    }
    static int64_t ABI_GetLength(void* thisptr) {
        Stream* obj = (Stream*)thisptr;
        return obj->GetLength();
    }
    static void ABI_Dispose(void* ptr) {
        delete (IDisposable*)ptr;
    }
    static std::string JoinFileComponents(std::vector<std::string> components, const std::string basePath,const std::string& separator) {
        std::string retval = basePath;
        //Strip out .. and . paths
        for (size_t i = 0; i < components.size(); i++) {
            if (components[i] == ".") {
                components.erase(components.begin() + i);
                i--;
            }
            else {
                if (components[i] == "..") {
                    if (i>0) {
                        components.erase(components.begin() + i - 1, components.begin() + i);
                        i -= 2;
                    }
                    else {
                        components.erase(components.begin() + i);
                        i--;
                    }
                }
                else {
                    retval += separator+ components[i];
                }
            }
        }
        return retval;
    }
    static std::vector<std::string> GetFileComponents(const std::string& path) {
        std::vector<std::string> retval;
        std::string current;
        for (size_t i = 0; i < path.size(); i++) {
            switch (path[i]) {
                case '/':
                    retval.push_back(current);
                    current = "";
                    break;
                default:
                    current += path[i];
                    break;
            }
        }
        if (current.size() != 0) {
            retval.push_back(current);
        }
        return retval;
    }
    
    class IFilesystem:public IDisposable {
    public:
        virtual void EnumerateNodes(const std::string& path, std::function<bool(const FileInformation&)> callback) {
            //Do nothing -- not implemented
            //printf("No V-table entry for EnumerateNodes\n");
        }
        virtual bool removexattr(const FileInformation& node, const char* key) = 0;
        virtual size_t getxattr(const FileInformation& node, const char* key, void* output, size_t bufferSize) = 0;
        virtual size_t listxattr(const FileInformation& node, char* list, size_t len) = 0;
        virtual void setxattr(const FileInformation& node, const char* key, const char* value, size_t size) = 0;
        virtual bool Exists(const std::string& filename) = 0;
        //WARNING: NOT ABI-SAFE!
        virtual Stream* GetStream(const std::string& filename) = 0;
        virtual void Stat(const std::string& filename, FileInformation& info) = 0;
        virtual void Mknod(const std::string& path, bool isFile) = 0;
        virtual void UpdateMetadata(const FileInformation& info) = 0;
        virtual void Delete(const std::string& path) {
            //Do nothing -- not implemented
        }
        virtual void Rename(const std::string& oldpath, const std::string& newpath) = 0;
        virtual ~IFilesystem(){};
    };
    static bool ABI_Exists(void* thisptr, const char* filename) {
        IFilesystem* mfs = (IFilesystem*)thisptr;
        return mfs->Exists(filename);
    }
    static void ABI_Truncate(void* thisptr, int64_t size) {
        Stream* obj = (Stream*)thisptr;
        obj->Truncate(size);
    }
    static void ManagedToNativeStream(Stream* managedStream, ::Stream* nativeStream) {
        
        nativeStream->Dispose = ABI_Dispose;
        nativeStream->QueryLength = ABI_GetLength;
        nativeStream->Read = ABI_Read;
        nativeStream->Write = ABI_Write;
        nativeStream->Truncate = ABI_Truncate;
        nativeStream->thisptr = managedStream;
    }
    static void ABI_GetStream(void* thisptr, const char* filename, ::Stream* str) {
        IFilesystem* mfs = (IFilesystem*)thisptr;
        Stream* sptr = mfs->GetStream(filename);
        ManagedToNativeStream(sptr, str);
        
    }
    static void ABI_Stat(void* thisptr, const char* filename, FileInformation* info) {
        IFilesystem* mfs = (IFilesystem*)thisptr;
        mfs->Stat(filename, *info);
    }
    static void ABI_Mknod(void* thisptr, const char* filename, bool isFile) {
        IFilesystem* mfs = (IFilesystem*)thisptr;
        mfs->Mknod(filename,isFile);
    }
    static void ABI_Enumerate(void* thisptr, const char* path,EnumerationCallback cb) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        fs->EnumerateNodes(path,[=](const FileInformation& info){
            return cb.callback(cb.thisptr,&info);
        });
        
    }
    static void ABI_Delete(void* thisptr, const char* path) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        fs->Delete(path);
    }
    static void ABI_Rename(void* thisptr,const char* oldname, const char* newname) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        fs->Rename(oldname,newname);
        
    }
    static void ABI_UpdateMetadata(void* thisptr, const FileInformation* info) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        fs->UpdateMetadata(*info);
    }
    /*
     virtual bool removexattr(const FileInformation& node, const char* key) = 0;
     virtual bool getxattr(const FileInformation& node, const char* key, void* output, size_t bufferSize) = 0;
     virtual size_t listxattr(const FileInformation& node, char* list, size_t len) = 0;
     virtual void setxattr(const FileInformation& node, const char* key, const char* value) = 0;
     
     */
    static bool ABI_removexattr(void* thisptr, const FileInformation* node, const char* key) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        return fs->removexattr(*node, key);
    }
    static size_t ABI_getxattr(void* thisptr, const FileInformation* node, const char* key, void* output, size_t buffersize) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        return fs->getxattr(*node, key, output, buffersize);
    }
    static size_t ABI_listxattr(void* thisptr,const FileInformation* node, char* list, size_t len) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        return fs->listxattr(*node, list, len);
        
    }
    static void ABI_setxattr(void* thisptr,const FileInformation* node, const char* key, const void* value, size_t size) {
        IFilesystem* fs = (IFilesystem*)thisptr;
        fs->setxattr(*node, key, (const char*)value,size);
        
    }
    static void ManagedToNativeFS(IFilesystem* fs, Filesystem* output) {
        output->Dispose = ABI_Dispose;
        output->Exists = ABI_Exists;
        output->GetStream = ABI_GetStream;
        output->Stat = ABI_Stat;
        output->Mknod = ABI_Mknod;
        output->EnumerateNodes = ABI_Enumerate;
        output->Delete = ABI_Delete;
        output->Rename = ABI_Rename;
        output->UpdateMetadata = ABI_UpdateMetadata;
        output->thisptr = fs;
        output->removexattr = ABI_removexattr;
        output->setxattr = ABI_setxattr;
        output->getxattr =ABI_getxattr;
        output->listxattr = ABI_listxattr;
    }
    static bool ABI_Enum(void* thisptr, const FileInformation* file) {
        std::function<bool(const FileInformation&)>* realfunc = (std::function<bool(const FileInformation&)>*)thisptr;
        return (*realfunc)(*file);
    }
    class ABI_Filesystem:public IFilesystem {
    public:
        ::Filesystem fs;
        void EnumerateNodes(const std::string& path, std::function<bool(const FileInformation&)> callback) {
            if(fs.EnumerateNodes) {
                EnumerationCallback cb;
                cb.callback = ABI_Enum;
                cb.thisptr = &callback;
                fs.EnumerateNodes(fs.thisptr,path.data(),cb);
            }else {
                printf("ERROR: No enumeration callback found\n");
            }
        }
        
        bool removexattr(const FileInformation& node, const char* key) {
            return fs.removexattr(fs.thisptr,&node,key);
        }
        size_t getxattr(const FileInformation& node, const char* key, void* output, size_t bufferSize) {
            return fs.getxattr(fs.thisptr,&node,key,output,bufferSize);
        }
        virtual size_t listxattr(const FileInformation& node, char* list, size_t len) {
            return fs.listxattr(fs.thisptr,&node,list,len);
        }
        void setxattr(const FileInformation& node, const char* key, const char* value, size_t size) {
            fs.setxattr(fs.thisptr,&node,key,value, size);
        }
        
        ABI_Filesystem(const ::Filesystem& nativeFS) {
            fs = nativeFS;
        }
        bool Exists(const std::string& filename) {
            return fs.Exists(fs.thisptr, filename.data());
        }
        void UpdateMetadata(const FileInformation& info) {
            fs.UpdateMetadata(fs.thisptr,&info);
        }
        Stream* GetStream(const std::string& filename) {
            ::Stream rstr;
            fs.GetStream(fs.thisptr, filename.data(), &rstr);
            return new ABI_Stream(rstr);
        }
        void Rename(const std::string& oldname, const std::string& newname) {
            fs.Rename(fs.thisptr,oldname.data(),newname.data());
        }
        void Stat(const std::string& filename, FileInformation& info) {
            if(fs.Stat) {
                fs.Stat(fs.thisptr, filename.data(), &info);
            }
        }
        void Mknod(const std::string& filename, bool isFile) {
            if(fs.Mknod) {
                fs.Mknod(fs.thisptr, filename.data(),isFile);
            }
        }
        void Delete(const std::string& path) {
            fs.Delete(fs.thisptr,path.data());
        }
        ~ABI_Filesystem() {
            fs.Dispose(fs.thisptr);
        }
    };
    
}
#endif

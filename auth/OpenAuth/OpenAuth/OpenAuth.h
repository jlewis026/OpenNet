// OpenAuth.h

#ifndef OpenNet_Auth
#define OpenNet_Auth
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    //The authority who signed the blob
    char* authority;
    //The blob
    unsigned char* blob;
    //The length of the blob
    size_t bloblen;
    unsigned char* signature;
    size_t siglen;
} NamedObject;
extern "C" {
    void* OpenNet_OAuthInitialize();
    void OpenNet_Retrieve(void* db, const char* name, void(*callback)(struct NamedObject* obj));
    bool AddObject(void* db, const char* name, const struct NamedObject* obj);
}



//OS-Specific routines
extern "C" {
	//Creates a 20-byte hash
	void* CreateHash();
	void UpdateHash(void* hash, const unsigned char* data, size_t sz);
	void FinalizeHash(void* hash, unsigned char* output);
	bool VerifySignature(unsigned char* data, size_t dlen, unsigned char* signature, size_t slen);
    size_t CreateSignature(const unsigned char* data, size_t dlen, unsigned char* privateKey, unsigned char* signature);
    bool isValidKey(unsigned char* data, size_t len, bool* isPrivate);
    unsigned char* CreatePrivateKey(size_t* len, size_t* pubLen);
}

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
#endif

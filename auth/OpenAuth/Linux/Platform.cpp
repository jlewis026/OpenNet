//This is what Platform.cpp looks like. Read over it and let me know if you have questions.
//So this is setting up the needs of the app on the linux platform?
//yes; this contains all necessary platform exports for Linux. what about ios?
//On iOS these functions need to be re-written. in obj-c/?
//Yes; they must be written in Objective-C.  so thats my job? Yes; for the first part of the project.
//Next part will be UI design. I'll let you do that too. ok sweet.
//Any questions as to how to get started on this?  nope not yet.  just rewriting to woek on ios
//Yes. It's not as trivial as it sounds.  I would assume not.
// This is the main DLL file.


#include "OpenAuth.h"

#include <crypto++/sha.h>
#include <crypto++/rsa.h>
#include <crypto++/osrng.h>

class IDisposable {
public:
	virtual ~IDisposable(){};
};
#define CP(val,type)((type*)val)
#define C(val,type)((type)val)
using namespace CryptoPP;
extern "C" {
	void* CreateHash() {
		return new SHA1();
	}
	void UpdateHash(void* hash, const unsigned char* data, size_t sz) {
		CP(hash, SHA1)->Update(data, sz);
	}
	void FinalizeHash(void* hash, unsigned char* output) {
		CP(hash, SHA1)->Final(output);
		delete CP(hash, SHA1);
	}

    void FreeByteArray(unsigned char* mander) {
        //Kill the poor Charmander......
        delete[] mander;
    }

    bool isValidKey(unsigned char* data, size_t len, bool* isPrivate) {
        *isPrivate = false;
        SafeBuffer buffer(data,len);
        unsigned char* buffy = new unsigned char[len];
        try {
            //Modulus
            uint32_t count;
            buffer.Read(count);
            buffer.Read(buffy,count);
            //Exponent
            buffer.Read(count);
            buffer.Read(buffy,count);
            //Private exponent
            buffer.Read(count);
            if(count) {
                buffer.Read(buffy,count);
                *isPrivate = true;
            }
            delete[] buffy;
            return true;
        }catch(const char* err) {
            delete[] buffy;
            return false;
        }
    }
    unsigned char* CreatePrivateKey(size_t* len, size_t* pubLen) {
        AutoSeededRandomPool rand;

        InvertibleRSAFunction params;
        params.GenerateRandomWithKeySize(rand,4096);
        const Integer& modulus = params.GetModulus();
        const Integer& exponent = params.GetPublicExponent();
        const Integer& private_exponent = params.GetPrivateExponent();
        size_t dlen = 4+modulus.MinEncodedSize()+4+exponent.MinEncodedSize()+4+private_exponent.MinEncodedSize();
        unsigned char* izard = new unsigned char[dlen];
        unsigned char* str = izard;
        uint32_t cval = modulus.MinEncodedSize();
        memcpy(str,&cval,4);
        str+=4;
        modulus.Encode(str,modulus.MinEncodedSize());
        str+=modulus.MinEncodedSize();
        cval = exponent.MinEncodedSize();
        memcpy(str,&cval,4);
        str+=4;
        exponent.Encode(str,exponent.MinEncodedSize());
        str+=exponent.MinEncodedSize();
        cval = private_exponent.MinEncodedSize();
        memcpy(str,&cval,4);
        str+=4;
        *pubLen = ((size_t)str-(size_t)izard);
        private_exponent.Encode(str,private_exponent.MinEncodedSize());
        str+=private_exponent.MinEncodedSize();
        *len = dlen;
        return izard;
    }

	bool VerifySignature(unsigned char* data, size_t dlen, unsigned char* signature, size_t slen) {
		RSASSA_PKCS1v15_SHA_Verifier verifier;
		return verifier.VerifyMessage(data, dlen, signature, slen);

	}
    size_t CreateSignature(const unsigned char* data, size_t dlen, unsigned char* privateKey, unsigned char* signature) {
        RSA::PrivateKey key;
        uint32_t len;
        memcpy(&len,privateKey,4);
        privateKey+=4;
        key.SetModulus(Integer(privateKey,len));
        privateKey+=len;
        memcpy(&len,privateKey,4);
        privateKey+=4;
        key.SetPublicExponent(Integer(privateKey,len));
        privateKey+=len;
        memcpy(&len,privateKey,4);
        privateKey+=4;
        key.SetPrivateExponent(Integer(privateKey,len));


        RSASSA_PKCS1v15_SHA_Signer signer(key);

		size_t mlen = signer.MaxSignatureLength();
		bool rst = false;
		if (signature == 0) {
			signature = new unsigned char[mlen];
			rst = true;
		}
        AutoSeededRandomPool generator;
        size_t retval = signer.SignMessage(generator, data, dlen, signature);
		if (rst) {
			delete[] signature;
		}
		return retval;
	}
}

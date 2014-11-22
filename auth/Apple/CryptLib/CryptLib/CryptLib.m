//
//  CryptLib.m
//  CryptLib
//
//  Created by Brian Bosak on 11/22/14.
//  Copyright (c) 2014 Brian Bosak. All rights reserved.
//
#import "CryptLib.h"
#include "OpenAuth.h"
@implementation CryptLib {
    void* db;
}
-(id)init {
    self = [super init];
    //Open database
    db = OpenNet_OAuthInitialize();
    
    return self;
}
-(void)dealloc {
    OpenNet_OAuthDestroy(db);
}
@end

void* CreateHash() {
    
}
void UpdateHash(void* hash, const unsigned char* data, size_t sz);
void FinalizeHash(void* hash, unsigned char* output);
bool VerifySignature(unsigned char* data, size_t dlen, unsigned char* signature, size_t slen);
size_t CreateSignature(const unsigned char* data, size_t dlen, unsigned char* privateKey, unsigned char* signature);
bool isValidKey(unsigned char* data, size_t len, bool* isPrivate);
unsigned char* CreatePrivateKey(size_t* len, size_t* pubLen);
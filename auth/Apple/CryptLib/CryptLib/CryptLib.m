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
@end

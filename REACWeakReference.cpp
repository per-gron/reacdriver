//
//  REACWeakReference.cpp
//  REAC
//
//  Created by Per Eckerdal on 11/03/2011.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#include "REACWeakReference.h"

#define super OSObject

OSDefineMetaClassAndStructors(REACWeakReference, OSObject)


bool REACWeakReference::initWithReference(void* reference) {
    ref = reference;
    return true;
}

REACWeakReference* REACWeakReference::withReference(void* reference) {
    REACWeakReference* r = new REACWeakReference;
    if (NULL == r) return NULL;
    bool result = r->initWithReference(reference);
    if (!result) {
        r->release();
        return NULL;
    }
    return r;
}

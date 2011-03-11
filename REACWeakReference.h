//
//  REACWeakReference.h
//  REAC
//
//  Created by Per Eckerdal on 11/03/2011.
//  Copyright (C) 2011 Per Eckerdal. All rights reserved.
//

#ifndef _REACWEAKREFERENCE_H
#define _REACWEAKREFERENCE_H

#include <IOKit/audio/IOAudioDevice.h>

#define REACWeakReference                com_pereckerdal_driver_REACWeakReference

class REACWeakReference : public OSObject {
    OSDeclareDefaultStructors(REACWeakReference)
    
public:
    void* ref;
    
    virtual bool initWithReference(void* reference);
    static REACWeakReference* withReference(void* reference);
};

#endif

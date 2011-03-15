/*
  File:REACDevice.cpp
*/

/*
    REAC is derived from Apple's 'PhantomAudioDriver'
    sample code.  It uses the same timer mechanism to simulate a hardware
    interrupt, with some additional code to compensate for the software
    timer's inconsistencies.  
    
    REAC basically copies the mixbuffer and presents it to clients
    as an input buffer, allowing applications to send audio one another.
*/

#include "REACDevice.h"

#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/IOLib.h>
#include <net/kpi_interface.h>

#include "REACAudioEngine.h"

#define super IOAudioDevice

OSDefineMetaClassAndStructors(REACDevice, IOAudioDevice)

bool REACDevice::init(OSDictionary *properties) {
    protocols = OSArray::withCapacity(5);
    if (NULL == protocols) {
        return false;
    }
    
    return super::init(properties);
}

bool REACDevice::initHardware(IOService *provider)
{
    bool result = false;
    
	// IOLog("REACDevice[%p]::initHardware(%p)\n", this, provider);
    
    if (!super::initHardware(provider))
        goto Done;
    
    setDeviceName("REAC by Per Eckerdal");
    setDeviceShortName("REAC by Per Eckerdal");
    setManufacturerName("Per Eckerdal");
    
    if (!createProtocolListeners())
        goto Done;
    
    result = true;
    
Done:

    return result;
}

void REACDevice::stop(IOService *provider)
{
    super::stop(provider);
    protocols->flushCollection();
}

void REACDevice::free() {
    if (NULL != protocols) {
        protocols->release();
    }
    
    super::free();
}

bool REACDevice::createProtocolListeners() {
    OSArray*				interfaceArray = OSDynamicCast(OSArray, getProperty(INTERFACES_KEY));
    OSCollectionIterator*	interfaceIterator;
    OSDictionary*			interfaceDict;
	
    if (!interfaceArray) {
        IOLog("REACDevice[%p]::createProtocolListeners() - Error: no Interface array in personality.\n", this);
        return false;
    }
    
	interfaceIterator = OSCollectionIterator::withCollection(interfaceArray);
    if (!interfaceIterator) {
		IOLog("REACDevice: no interfaces to listen to available.\n");
		return true;
	}
    
    while ((interfaceDict = (OSDictionary*)interfaceIterator->getNextObject())) {
        OSString       *ifname = OSDynamicCast(OSString, interfaceDict->getObject(INTERFACE_NAME_KEY));
		REACConnection *protocol = NULL;
        ifnet_t interface;
        
        if (NULL == ifname) {
            IOLog("REACDevice: Invalid interface entry in personality (no string name).\n");
            goto Next;
        }
        
        if (0 != ifnet_find_by_name(ifname->getCStringNoCopy(), &interface)) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to find interface '%s'.\n",
                  this, ifname->getCStringNoCopy());
            goto Next;
        }
        
        protocol = REACConnection::withInterface(getWorkLoop(),
                                                 interface,
                                                 REACConnection::REAC_SPLIT,
                                                 &REACDevice::connectionCallback,
                                                 &REACDevice::samplesCallback,
                                                 &REACDevice::getSamplesCallback,
                                                 this, // Cookie A (the REACAudioDevice)
                                                 NULL); // Cookie B (the REACAudioEngine)
        ifnet_release(interface);
        
        if (NULL == protocol) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to initialize REAC listener for '%s'.\n",
                  this, ifname->getCStringNoCopy());
            goto Next;
        }
        
        if (!protocol->listen()) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to listen to '%s'.\n",
                  this, ifname->getCStringNoCopy());
            goto Next;
        }
        
        if (!protocols->setObject(protocol)) {
            IOLog("REACDevice[%p]::createProtocolListeners(): Failed to insert protocol listener '%s' into array.\n",
                  this, ifname->getCStringNoCopy());
            goto Next;
        }
        
    Next:
        if (NULL != protocol) {
            protocol->release();
        }
    }
	
    interfaceIterator->release();
    return true;
}

void REACDevice::connectionCallback(REACConnection *proto, void **cookieA, void** cookieB, REACDeviceInfo *deviceInfo) {
    REACDevice *device = (REACDevice*) *cookieA;
    
    REACAudioEngine *engine = (REACAudioEngine*) *cookieB;
    
    // We need to stop the audio engine regardless of whether it's a connect or a disconnect;
    // when connecting, we want to make sure to stop any old instance just in case.
    if (NULL != engine) {
        engine->stopAudioEngine();
        *cookieB = NULL;
    }
    
    if (NULL == deviceInfo) {
        IOLog("REACDevice[%p]::connectionCallback() - Disconnected.\n", device);
    }
    else {
        // IOLog("REACDevice[%p]::connectionCallback() - Connected.\n", device);
        
        *cookieB = (void*) device->createAudioEngine(proto);
    }
}

void REACDevice::samplesCallback(REACConnection *proto, void **cookieA, void** cookieB, UInt8 **data, UInt32 *bufferSize) {
    // IOLog("REACDevice[%p]::samplesCallback()\n", *cookieA);
    
    REACAudioEngine *engine = (REACAudioEngine *)*cookieB;
    if (NULL != engine) {
        engine->gotSamples(data, bufferSize);
    }
}

void REACDevice::getSamplesCallback(REACConnection *proto, void **cookieA, void** cookieB, UInt8 **data, UInt32 *bufferSize) {
    // IOLog("REACDevice[%p]::samplesCallback()\n", *cookieA);
    
    REACAudioEngine *engine = (REACAudioEngine *)*cookieB;
    if (NULL != engine) {
        engine->getSamples(data, bufferSize);
    }
}

REACAudioEngine* REACDevice::createAudioEngine(REACConnection *proto)
{
    OSDictionary *originalAudioEngineParams = OSDynamicCast(OSDictionary, getProperty(AUDIO_ENGINE_PARAMS_KEY));
    OSDictionary *audioEngineParams = NULL;
    
    ifnet_t interface = proto->getInterface();
    u_int32_t unitNumber = ifnet_unit(interface);
    const char* ifname = ifnet_name(interface);
	
    if (!originalAudioEngineParams) {
        IOLog("REACDevice[%p]::createAudioEngine() - Error: no AudioEngine parameters in personality.\n", this);
        return NULL;
    }
    
    audioEngineParams = OSDynamicCast(OSDictionary, originalAudioEngineParams->copyCollection());
    if (NULL == audioEngineParams) {
        return NULL;
    }
    
    OSString* desc = OSDynamicCast(OSString, audioEngineParams->getObject(DESCRIPTION_KEY));
    if (NULL != desc) {
        char buf[100];
        snprintf(buf, sizeof(buf), "%s (%s%d)", desc->getCStringNoCopy(), ifname, unitNumber);
        OSString* newName = OSString::withCStringNoCopy(buf);
        if (newName) {
            audioEngineParams->setObject(DESCRIPTION_KEY, newName);
            newName->release();
        }
    }

    REACAudioEngine* audioEngine = new REACAudioEngine;
    if (!audioEngine) {
        goto Done;
    }
    
    if (!audioEngine->init(proto, audioEngineParams)) {
        IOLog("REACDevice[%p]::createAudioEngine() - Error: Failed to init audio engine.\n", this);
        audioEngine->release();
        audioEngine = NULL;
        goto Done;
    }
    
    if (kIOReturnSuccess != activateAudioEngine(audioEngine)) { // increments refcount and manages the object
        IOLog("REACDevice[%p]::createAudioEngine() - Error: Failed to activate audio engine.\n", this);
        audioEngine->release();
        audioEngine = NULL;
        goto Done;
    }
    
    
    audioEngine->release();				// decrement refcount so object is released when the manager eventually releases it
    
Done:
    if (NULL != audioEngineParams) {
        audioEngineParams->release();
    }
    
    return audioEngine;
}

IOReturn REACDevice::performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
                                             IOAudioDevicePowerState newPowerState, 
                                             UInt32 *microsecondsUntilComplete) {
    // TODO
    return kIOReturnSuccess;
}

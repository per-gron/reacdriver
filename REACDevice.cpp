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

const SInt32 REACDevice::kVolumeMax = 65535;
const SInt32 REACDevice::kGainMax = 65535;


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
		REACProtocol   *protocol = NULL;
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
        
        protocol = REACProtocol::withInterface(interface,
                                               REACProtocol::REAC_SPLIT,
                                               &REACDevice::connectionCallback,
                                               &REACDevice::samplesCallback,
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

void REACDevice::samplesCallback(REACProtocol *proto, void **cookieA, void** cookieB, int numSamples, UInt8 *samples) {
    // IOLog("REACDevice[%p]::samplesCallback()\n", *cookieA);
    
    REACAudioEngine *engine = (REACAudioEngine*) *cookieB;
    if (NULL != engine) {
        engine->gotSamples(numSamples, samples);
    }
}

void REACDevice::connectionCallback(REACProtocol *proto, void **cookieA, void** cookieB, REACDeviceInfo *deviceInfo) {
    REACDevice *device = (REACDevice*) *cookieA;
    
    REACAudioEngine *engine = (REACAudioEngine*) *cookieB;
    
    // We need to stop the audio engine regardless of whether it's a connect or a disconnect;
    // when connecting, we want to make sure to stop any old instance just in case.
    if (NULL != engine) {
        engine->performAudioEngineStop();
        *cookieB = NULL;
    }
    
    if (NULL == deviceInfo) {
        // IOLog("REACDevice[%p]::connectionCallback() - Disconnected.\n", device);
    }
    else {
        // IOLog("REACDevice[%p]::connectionCallback() - Connected.\n", device);
        
        *cookieB = (void*) device->createAudioEngine(proto);
    }
}


REACAudioEngine* REACDevice::createAudioEngine(REACProtocol* proto)
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
        audioEngine->release();
        audioEngine = NULL;
        goto Done;
    }
    
    initControls(audioEngine);
    activateAudioEngine(audioEngine);	// increments refcount and manages the object
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
    return kIOReturnSuccess;
}


#define addControl(control, handler) \
    if (!control) {\
		IOLog("REAC failed to add control.\n");	\
		return false; \
	} \
    control->setValueChangeHandler(handler, this); \
    audioEngine->addDefaultAudioControl(control); \
    control->release();

bool REACDevice::initControls(REACAudioEngine* audioEngine)
{
    IOAudioControl*	control = NULL;
    
    for (UInt32 channel=0; channel <= 16; channel++) {
        mVolume[channel] = mGain[channel] = 65535;
        mMuteOut[channel] = mMuteIn[channel] = false;
    }
    
    const char *channelNameMap[17] = {	kIOAudioControlChannelNameAll,
										kIOAudioControlChannelNameLeft,
										kIOAudioControlChannelNameRight,
										kIOAudioControlChannelNameCenter,
										kIOAudioControlChannelNameLeftRear,
										kIOAudioControlChannelNameRightRear,
										kIOAudioControlChannelNameSub };
	
    for (UInt32 channel=7; channel <= 16; channel++)
        channelNameMap[channel] = "Unknown Channel";
    
    for (unsigned channel=0; channel <= 16; channel++) {
		
        // Create an output volume control for each channel with an int range from 0 to 65535
        // and a db range from -72 to 0
        // Once each control is added to the audio engine, they should be released
        // so that when the audio engine is done with them, they get freed properly
        control = IOAudioLevelControl::createVolumeControl(REACDevice::kVolumeMax,		// Initial value
                                                           0,									// min value
                                                           REACDevice::kVolumeMax,		// max value
                                                           (-72 << 16) + (32768),				// -72 in IOFixed (16.16)
                                                           0,									// max 0.0 in IOFixed
                                                           channel,								// kIOAudioControlChannelIDDefaultLeft,
                                                           channelNameMap[channel],				// kIOAudioControlChannelNameLeft,
                                                           channel,								// control ID - driver-defined
                                                           kIOAudioControlUsageOutput);
        addControl(control, (IOAudioControl::IntValueChangeHandler)volumeChangeHandler);
        
        // Gain control for each channel
        control = IOAudioLevelControl::createVolumeControl(REACDevice::kGainMax,			// Initial value
                                                           0,									// min value
                                                           REACDevice::kGainMax,			// max value
                                                           0,									// min 0.0 in IOFixed
                                                           (72 << 16) + (32768),				// 72 in IOFixed (16.16)
                                                           channel,								// kIOAudioControlChannelIDDefaultLeft,
                                                           channelNameMap[channel],				// kIOAudioControlChannelNameLeft,
                                                           channel,								// control ID - driver-defined
                                                           kIOAudioControlUsageInput);
        addControl(control, (IOAudioControl::IntValueChangeHandler)gainChangeHandler);
    }
	
    // Create an output mute control
    control = IOAudioToggleControl::createMuteControl(false,									// initial state - unmuted
                                                      kIOAudioControlChannelIDAll,				// Affects all channels
                                                      kIOAudioControlChannelNameAll,
                                                      0,										// control ID - driver-defined
                                                      kIOAudioControlUsageOutput);
    addControl(control, (IOAudioControl::IntValueChangeHandler)outputMuteChangeHandler);
    
    // Create an input mute control
    control = IOAudioToggleControl::createMuteControl(false,									// initial state - unmuted
                                                      kIOAudioControlChannelIDAll,				// Affects all channels
                                                      kIOAudioControlChannelNameAll,
                                                      0,										// control ID - driver-defined
                                                      kIOAudioControlUsageInput);
    addControl(control, (IOAudioControl::IntValueChangeHandler)inputMuteChangeHandler);
    
    return true;
}


IOReturn REACDevice::volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn			result = kIOReturnBadArgument;
    REACDevice*	audioDevice = (REACDevice *)target;
	
    if (audioDevice)
        result = audioDevice->volumeChanged(volumeControl, oldValue, newValue);
    return result;
}


IOReturn REACDevice::volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue)
{
    if (volumeControl)
         mVolume[volumeControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACDevice::outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn			result = kIOReturnBadArgument;
    REACDevice*	audioDevice = (REACDevice*)target;
	
    if (audioDevice)
        result = audioDevice->outputMuteChanged(muteControl, oldValue, newValue);
    return result;
}


IOReturn REACDevice::outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    if (muteControl)
         mMuteOut[muteControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACDevice::gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn			result = kIOReturnBadArgument;
    REACDevice*	audioDevice = (REACDevice *)target;
	
    if (audioDevice)
        result = audioDevice->gainChanged(gainControl, oldValue, newValue);
    return result;
}


IOReturn REACDevice::gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue)
{
    if (gainControl)
		mGain[gainControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACDevice::inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn			result = kIOReturnBadArgument;
    REACDevice*	audioDevice = (REACDevice*)target;

    if (audioDevice)
        result = audioDevice->inputMuteChanged(muteControl, oldValue, newValue);
    return result;
}


IOReturn REACDevice::inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    if (muteControl)
         mMuteIn[muteControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}

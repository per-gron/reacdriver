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


bool REACDevice::initHardware(IOService *provider)
{
    bool result = false;
    
	//IOLog("REACDevice[%p]::initHardware(%p)\n", this, provider);
    
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
            continue;
        }
        
        if (0 != ifnet_find_by_name(ifname->getCStringNoCopy(), &interface)) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to find interface '%s'.\n",
                  this, ifname->getCStringNoCopy());
            continue;
        }
        
        protocol = REACProtocol::withInterface(interface,
                                               REACProtocol::REAC_SPLIT,
                                               &REACDevice::connectionCallback,
                                               this, // Connection cookie
                                               &REACDevice::samplesCallback,
                                               this); // Samples cookie
        ifnet_release(interface);
        
        if (NULL == protocol) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to initialize REAC listener for '%s'.\n",
                  this, ifname->getCStringNoCopy());
            continue;
        }
        
        if (!protocol->listen()) {
            IOLog("REACDevice[%p]::createProtocolListeners() - Error: failed to listen to '%s'.\n",
                  this, ifname->getCStringNoCopy());
            continue;
        }
        
        protocol->detach();
        protocol->release();
    }
	
    interfaceIterator->release();
    return true;
}

void REACDevice::samplesCallback(REACProtocol* proto, void* cookie, int numSamples, UInt8* samples) {
    IOLog("REACDevice[%p]::samplesCallback()\n", cookie);
}

void REACDevice::connectionCallback(REACProtocol* proto, void* cookie, REACDeviceInfo* device) {
    if (NULL == device) {
        IOLog("REACDevice[%p]::connectionCallback() - Disconnected.\n", cookie);
    }
    else {
        IOLog("REACDevice[%p]::connectionCallback() - Connected.\n", cookie);
    }
}

void REACDevice::stop(IOService *provider)
{
    super::stop(provider);
    if (NULL != proto) proto->detach();
}


bool REACDevice::createAudioEngines()
{
    OSArray*				audioEngineArray = OSDynamicCast(OSArray, getProperty(AUDIO_ENGINES_KEY));
    OSCollectionIterator*	audioEngineIterator;
    OSDictionary*			audioEngineDict;
	
    if (!audioEngineArray) {
        IOLog("REACDevice[%p]::createAudioEngine() - Error: no AudioEngine array in personality.\n", this);
        return false;
    }
    
	audioEngineIterator = OSCollectionIterator::withCollection(audioEngineArray);
    if (!audioEngineIterator) {
		IOLog("REACDevice: no audio engines available.\n");
		return true;
	}
    
    while ((audioEngineDict = (OSDictionary*)audioEngineIterator->getNextObject())) {
		REACAudioEngine*	audioEngine = NULL;
		
        if (OSDynamicCast(OSDictionary, audioEngineDict) == NULL)
            continue;
        
		audioEngine = new REACAudioEngine;
        if (!audioEngine)
			continue;
        
        if (!audioEngine->init(audioEngineDict))
			continue;

		initControls(audioEngine);
        activateAudioEngine(audioEngine);	// increments refcount and manages the object
        audioEngine->release();				// decrement refcount so object is released when the manager eventually releases it
    }
	
    audioEngineIterator->release();
    return true;
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

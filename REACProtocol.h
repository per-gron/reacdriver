/*
 *  REACProtocol.h
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACPROTOCOL_H
#define _REACPROTOCOL_H

#include <IOKit/audio/IOAudioDevice.h>

#include <libkern/OSMalloc.h>
#include <net/kpi_interface.h>

# if defined(__cplusplus)
#  define RT_C_DECLS_BEGIN extern "C" {
#  define RT_C_DECLS_END   }
# else
#  define RT_C_DECLS_BEGIN
#  define RT_C_DECLS_END
# endif

RT_C_DECLS_BEGIN /* Buggy 10.4 headers, fixed in 10.5. */
#include <sys/kpi_mbuf.h>
#include <net/kpi_interfacefilter.h>
RT_C_DECLS_END


#define REAC_MAX_CHANNEL_COUNT 40
#define REAC_PACKETS_PER_SECOND 8000
#define REAC_RESOLUTION 3 // 3 bytes per sample per channel
#define REAC_SAMPLES_PER_PACKET 12
#define ETHER_ADDR_LEN 6

#define REAC_ENDING 0xeac2 // TODO Endianness?


#define EthernetHeader				com_pereckerdal_driver_EthernetHeader
#define REACPacketHeader            com_pereckerdal_driver_REACPacketHeader
#define REACDeviceInfo              com_pereckerdal_driver_REACDeviceInfo
#define REACProtocol                com_pereckerdal_driver_REACProtocol


/* Ethernet header */
struct EthernetHeader {
	UInt8 dhost[ETHER_ADDR_LEN]; /* Destination host address */
	UInt8 shost[ETHER_ADDR_LEN]; /* Source host address */
	UInt16 type; /* IP? ARP? RARP? etc */
};

/* REAC packet header */
struct REACPacketHeader {
    UInt16 counter;
    UInt16 type;
    UInt16 data[16];
};

struct REACDeviceInfo {
    UInt8 addr[ETHER_ADDR_LEN];
    UInt32 in_channels;
    UInt32 out_channels;
};

class REACProtocol;

// Is only called when the connection callback has indicated that there is a connection
//   When in REAC_SLAVE mode, this function is expected to overwrite the samples parameter
// with the output data [Note to self: if I do this, make sure that the buffer is big enough]
typedef void(*reac_samples_callback_t)(REACProtocol* proto, void* cookie, UInt8* samples[REAC_SAMPLES_PER_PACKET]);
// Device is NULL on disconnect
typedef void(*reac_connection_callback_t)(REACProtocol* proto, void* cookie, REACDeviceInfo* device);


// TODO Thread safety?
// TODO Private constructor/assignment operator/destructor?
class REACProtocol {
public:
    enum REACMode {
        REAC_MASTER, REAC_SLAVE, REAC_SPLIT
    };
    
    enum REACStreamType { // TODO Endianness?
        REAC_STREAM_FILLER = 0,
        REAC_STREAM_CONTROL = 0xeacd,
        REAC_STREAM_CONTROL2 = 0xeacf,
        REAC_STREAM_FROM_SPLIT = 0xeace
    };
    
    static REACProtocol* listen(ifnet_t interface, REACMode mode,
                                reac_samples_callback_t samplesCallback,
                                void* samplesCookie,
                                reac_connection_callback_t connectionCallback,
                                void* connectionCookie);
    void detach();
    
    // When in REAC_MASTER mode, this function is expected to be called
    // REAC_PACKETS_PER_SECOND times per second (on average).
    //   Returns 0 on success, EINVAL when mode is not REAC_MASTER
    errno_t pushSamples(UInt8 *samples[REAC_SAMPLES_PER_PACKET]);
    
    const REACDeviceInfo* getDeviceInfo() const;
    bool isConnected() const;

protected:
    REACMode mode;
    interface_filter_t  filterRef;
    OSMallocTag         reacMallocTag;
    UInt16              lastCounter;
    
    bool                connected;
    REACDeviceInfo     *deviceInfo;
    
    reac_samples_callback_t samplesCallback;
    void* samplesCookie;
    reac_connection_callback_t connectionCallback;
    void* connectionCookie;
    
    static errno_t filterInputFunc(void *cookie,
                                   ifnet_t interface, 
                                   protocol_family_t protocol,
                                   mbuf_t *data,
                                   char **frame_ptr);        
    static void filterDetachedFunc(void *cookie,
                                   ifnet_t interface); 
    
    void processDataStream(const REACPacketHeader* packet);
    bool checkChecksum(const REACPacketHeader* packet) const; // For data stream packets
    
};


#endif // _REACPROTOCOL_H

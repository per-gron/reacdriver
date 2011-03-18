/*
 *  EthernetHeader.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _ETHERNETHEADER_H
#define _ETHERNETHEADER_H

#define EthernetHeader              com_pereckerdal_driver_EthernetHeader

/* Ethernet header */
struct EthernetHeader {
	UInt8 dhost[ETHER_ADDR_LEN]; /* Destination host address */
	UInt8 shost[ETHER_ADDR_LEN]; /* Source host address */
	UInt8 type[2]; /* IP? ARP? RARP? etc */
};

#endif

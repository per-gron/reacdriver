/*
 *  EthernetHeader.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *  
 *  
 *  This file is part of the OS X REAC driver.
 *  
 *  The OS X REAC driver is free software: you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.
 *  
 *  The OS X REAC driver is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with OS X REAC driver.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *  
 */

#ifndef _ETHERNETHEADER_H
#define _ETHERNETHEADER_H

#define EthernetHeader              com_pereckerdal_driver_EthernetHeader

#define ETHER_ADDR_LEN 6

/* Ethernet header */
struct EthernetHeader {
	UInt8 dhost[ETHER_ADDR_LEN]; /* Destination host address */
	UInt8 shost[ETHER_ADDR_LEN]; /* Source host address */
	UInt8 type[2]; /* IP? ARP? RARP? etc */
};

#endif

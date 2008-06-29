/*
 * Copyright (C) 2003 CTIE, Monash University
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef __ETHERHUB_H
#define __ETHERHUB_H

#include "INETDefs.h"

/**
 * Models a wiring hub. It simply broadcasts the received message
 * on all other ports.
 */
class INET_API EtherHub : public cSimpleModule
{
  protected:
    int ports;          // number of ports
    long numMessages;   // number of messages handled

  protected:
    virtual void initialize();
    virtual void handleMessage(cMessage*);
    virtual void finish();
};

#endif


//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "ONUQPerLLiDBase.h"


ONUQPerLLiDBase::ONUQPerLLiDBase(){
	regTOMsg=0;
	regSendMsg=0;
}
ONUQPerLLiDBase::~ONUQPerLLiDBase(){
	cancelAndDelete(regSendMsg);
	cancelAndDelete(regTOMsg);

	for (uint32_t i=0; i<pon_queues.size(); i++){
		pon_queues[i].clean();
	}
	pon_queues.clear();
}

void ONUQPerLLiDBase::initialize()
{
	numOfLLIDs=1;
	serviceList=NULL;

	if (dynamic_cast<ServiceConfig *>( findModuleUp("serviceConfig")) != NULL){
		serviceList = &(dynamic_cast<ServiceConfig *>( findModuleUp("serviceConfig"))->srvs);
		// 1 llid per service
		numOfLLIDs = serviceList->size();
	}
	// Parameters
	regTimeOut = par("regTimeOut");
	regTimeOut/=1000;
	queueLimit = par("queueLimit");


	regTOMsg = new cMessage("regTOMsg", REGTOMSG);
	regSendMsg = new cMessage("regSendMsg", REGSENDMSG);


	// Create Q for BroadCasts
	QueuePerLLid tmp_qpllid;
	tmp_qpllid.setServiceName("BroadCast");
	tmp_qpllid.prior = 0.0;
	tmp_qpllid.llid = LLID_EPON_BC;
	tmp_qpllid.queueLimit = queueLimit;
	addSortedLLID(tmp_qpllid);

	WATCH_VECTOR(pon_queues);
	allQsEmpty=true;
	nextQIndex=0;
}

void ONUQPerLLiDBase::handleMessage(cMessage *msg)
{

	// Self Message
	if (msg->isSelfMessage())
	{
		EV << "Self-message " << msg << " received\n";

		if (msg == regTOMsg){
			EV << "*** ONUMacCtl: Registration FAILED"<<endl;
		}else if (msg == regSendMsg)
			sendMPCPReg();
		else
			error("Unknown self message received!");

		return;
	}


	// Network Message
	cGate *ingate = msg->getArrivalGate();
	EV << "Frame " << msg << " arrived on port " << ingate->getName() << "...\n";

	if (ingate->getId() ==  gate( "lowerLayerIn")->getId()){
		processFrameFromLowerLayer(msg);
	}
	else if (ingate->getId() ==  gate( "upperLayerIn")->getId()){
		// Add the frame to the proper Q
		processFrameFromHigherLayer(msg);
	}else{
		EV << "ONUMacCtl: Message came FROM THE WRONG DIRRECTION???? Dropping\n";
		delete msg;
	}

}


void ONUQPerLLiDBase::processFrameFromHigherLayer(cMessage *msg){
	EV << "ONUQPerLLiDBase: Incoming from higher layer...\n";


	//TODO: Add to Q
	EPON_LLidCtrlInfo *nfo = dynamic_cast<EPON_LLidCtrlInfo *>(msg->getControlInfo());

	// If frame has no info or is BC add to the BroadCast Q
	int Q_index=-1;
	if (!nfo || nfo->llid == LLID_EPON_BC){
		Q_index = getIndexForLLID(LLID_EPON_BC);
	}else{
		Q_index = getIndexForLLID(nfo->llid);
	}

	// Check if we can forward frame
	if (Q_index == -1){
		EV << "*** WRONG LLID : DROPPING" <<endl;
		delete msg;
		return;
	}

	// Update the incoming rate BEFORE discarding message
	// in case the Q is full.
	cPacket *pkt = dynamic_cast<cPacket *>(msg);
	pon_queues[Q_index].vec->numIncomingBits+=pkt->getBitLength();

	// Check that is not full
	if (pon_queues[Q_index].length() >= pon_queues[Q_index].queueLimit){
		pon_queues[Q_index].vec->numBytesDropped+=pkt->getByteLength();
		pon_queues[Q_index].vec->recordVectors();
		delete msg;
		return;
	}else{
		// Record the incoming bytes
		pon_queues[Q_index].vec->recordVectors();
	}


	// Finally add to the Q
	pon_queues[Q_index].insert(msg);


	// NOTIFY MAC
	if (allQsEmpty){
		allQsEmpty = false;
		send(new cMessage("WakeUpYouBitch", WAKEUPMSG), "lowerLayerOut");
	}
}

void ONUQPerLLiDBase::processFrameFromLowerLayer(cMessage *msg){
	EV << "ONUQPerLLiDBase: Incoming from lower layer...\n";

	EthernetIIFrame * frame = dynamic_cast<EthernetIIFrame *>(msg);


	if (frame && frame->getEtherType() == MPCP_TYPE){
		processMPCP(frame );
		return;
	}

	// Drop if it is not for us based on LLID
	if (!checkLLIDforUs(msg)) {
		delete msg;
		return;
	}

	send(msg,"upperLayerOut");
}

bool ONUQPerLLiDBase::checkLLIDforUs(cMessage *msg){
	// Drop if it is not for us based on LLID
	EPON_LLidCtrlInfo * nfo =  dynamic_cast<EPON_LLidCtrlInfo *>(msg->getControlInfo());
	// Check for BC
	if (nfo && nfo->llid != LLID_EPON_BC){
		// -1 = No such llid
		if (getIndexForLLID(nfo->llid) == -1) {
			return false;
		}
	}
	return true;
}


void ONUQPerLLiDBase::processMPCP(EthernetIIFrame *frame ){
	EV << "ONUMacCtl: MPCP Frame processing\n";
	MPCP * mpcp = check_and_cast<MPCP *>(frame);


	switch (mpcp->getOpcode())
	{
		case MPCP_REGISTER:
		{

			MPCPRegAck *ack = new MPCPRegAck();
			MPCPRegister * reg = check_and_cast<MPCPRegister *>(frame);

			EV << "ONUMacCtl: Type is MPCP_REGISTER\n";
			cancelEvent(regTOMsg);
			EV << "ONUMacCtl: Canceling RegTOMsg\n";

			ack->setOpcode(MPCP_REGACK);
			ack->setName("MPCPAck");
			ack->setEtherType(MPCP_TYPE);
			ack->setDest(mpcp->getSrc());
			ack->setByteLength(MPCP_HEADER_LEN);

			int LLID_num=reg->getPtpNumReg();

			EV<< "Assigned LLIDs:" <<LLID_num<<endl;
			for (uint8_t i=0; i<reg->getLLIDsArraySize(); i++){
				// 0xFFF = 4095 intrand -> [0-4095)
				EV<< (int)i<<"  " <<(int)reg->getLLIDs(i)<<endl;
				QueuePerLLid tmp_qpllid;
				tmp_qpllid.llid = reg->getLLIDs(i);
				std::string tmp_name="Default";
				tmp_qpllid.prior = 0.0;
				if (serviceList) {
					tmp_name=serviceList->at(i).name;
					EV<<tmp_name<<" -- "<<reg->getLLIDs(i)<<endl;
					tmp_qpllid.prior = serviceList->at(i).priority;
				}

				tmp_qpllid.setServiceName(tmp_name);
				tmp_qpllid.queueLimit = queueLimit;
				addSortedLLID(tmp_qpllid);



			}

			send(ack,"lowerLayerOut");
			// Send the frame on top layer that manages LLIDs
			send(frame->dup(),"upperLayerOut");

			break;
		}
		case MPCP_GATE:
		{
			MPCPGate * gate = check_and_cast<MPCPGate *>(frame);
			EV << "ONUQPerLLiDBase: Type is MPCP_GATE\n";


			// Register Grant
			if (gate->getListLen() == 1
				&& gate->getDest().isBroadcast())
			{
				EV << "ONUMacCtl: MPCP REGISTER GRANT (DOOUUU)" <<endl;
				// Process here number of llids...
				// this module's init may be called before serviceConfig
				if (serviceList) numOfLLIDs = serviceList->size();
				startMPCPReg(gate->getDuration(0));
				break;
			}

			break;
		}
		default:
			EV << "ONUMacCtl: Unrecognized MPCP OpCode!!\n";
			return;
	};

	delete frame;
}


void ONUQPerLLiDBase::sendMPCPReg(){
	MPCPRegReq *regreq = new MPCPRegReq();
	regreq->setDest(MACAddress("FF:FF:FF:FF:FF:FF"));
	regreq->setEtherType(MPCP_TYPE);
	regreq->setName("MPCPRegReq");
	regreq->setOpcode(MPCP_REGREQ);
	regreq->setPtpNumReq(numOfLLIDs);

	regreq->setByteLength(MPCP_HEADER_LEN+MPCP_LIST_LEN);

	// Send REG REQ and Schedule a timeout
	send(regreq, "lowerLayerOut");
	scheduleAt(simTime() +regTimeOut, regTOMsg );
}



void ONUQPerLLiDBase::startMPCPReg(uint32_t regMaxRandomSleep){

	uint32_t rndBackOff = dblrand() * regMaxRandomSleep;
	EV<<"Sending MPCP REGREQ in " << rndBackOff << "(< " <<regMaxRandomSleep<<")"<< endl;
	simtime_t t;
	t.setRaw(simTime().raw() + MPCPTools::ns16ToSimTime(rndBackOff));
	scheduleAt(t, regSendMsg );
}

int ONUQPerLLiDBase::getIndexForLLID(uint16_t llid){
	for (uint32_t i=0; i<pon_queues.size(); i++)
		if (pon_queues[i].llid == llid) return i;

	return -1;
}

void ONUQPerLLiDBase::addSortedLLID(QueuePerLLid tmp_qpllid){
	// Just add it if empty...
	if (pon_queues.empty()) {
		pon_queues.push_back(tmp_qpllid);
		return;
	}

	// Add in the proper possition
	for (PonQueues::iterator it = pon_queues.begin(); it != pon_queues.end(); it++){
		if ((*it).prior < tmp_qpllid.prior){
			pon_queues.insert(it, tmp_qpllid);
			return;
		}
	}

	// If not added till here... add to the end
	pon_queues.push_back(tmp_qpllid);
}

/**
 * Check is all the Qs are empty. IF yes set
 * the all empty variable. This must be done after
 * the frame request from the lower layer.
 */
void ONUQPerLLiDBase::checkIfAllEmpty(){
	if (pon_queues.size() == 0) allQsEmpty = true;
	if (pon_queues[0].isEmpty()
			&& pon_queues.size() == 1) allQsEmpty = true;


	bool found=false;
	// if the current Q is empty -> find the next one
	if (pon_queues[nextQIndex].isEmpty()){
		for (int i=0; i<(int)pon_queues.size(); i++){
			if (!pon_queues[i].isEmpty()) {
				found=true;
				break;
			}
		}
	// If not empty.. we have a frame
	}else found=true;

	allQsEmpty = !found;
}

cModule * ONUQPerLLiDBase::findModuleUp(const char * name){
	cModule *mod = NULL;
	for (cModule *curmod=this; !mod && curmod; curmod=curmod->getParentModule())
	     mod = curmod->getSubmodule(name);
	return mod;
}


void ONUQPerLLiDBase::finish(){
	simtime_t t = simTime();

	for (uint32_t i=0; i<pon_queues.size(); i++){
		std::string srvName=pon_queues[i].getServiceName();
		std::string name=srvName+" BytesDropped";
		recordScalar(name.c_str(), pon_queues[i].vec->numBytesDropped  );
		name=srvName+" BytesSent";
		recordScalar(name.c_str(), pon_queues[i].vec->numBytesSent  );

		if (t>0)
		{
			name=srvName+" bytes dropped/sec";
			recordScalar(name.c_str(), pon_queues[i].vec->numBytesDropped/t);
			name=srvName+" bytes sent/sec";
			recordScalar(name.c_str(), pon_queues[i].vec->numBytesSent/t);
		}
	}
}



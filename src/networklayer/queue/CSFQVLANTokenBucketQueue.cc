//
// Copyright (C) 2013 Kyeong Soo (Joseph) Kim
// Copyright (C) 2005 Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//


#include "CSFQVLANTokenBucketQueue.h"

Define_Module(CSFQVLANTokenBucketQueue);

CSFQVLANTokenBucketQueue::CSFQVLANTokenBucketQueue()
{
}

CSFQVLANTokenBucketQueue::~CSFQVLANTokenBucketQueue()
{
}

void CSFQVLANTokenBucketQueue::initialize()
{
    PassiveQueueBase::initialize();

    outGate = gate("out");

    // general
    numFlows = par("numFlows"); // also the number of subscribers

    // VLAN classifier
    const char *classifierClass = par("classifierClass");
    classifier = check_and_cast<IQoSClassifier *>(createOne(classifierClass));
    classifier->setMaxNumQueues(numFlows);
    const char *vids = par("vids");
    classifier->initialize(vids);

    // Token bucket meters
    tbm.assign(numFlows, (BasicTokenBucketMeter *)NULL);
    cModule *mac = getParentModule();
    for (int i=0; i<numFlows; i++)
    {
        cModule *meter = mac->getSubmodule("meter", i);
        tbm[i] = check_and_cast<BasicTokenBucketMeter *>(meter);
    }

    // FIFO queue
    queueSize = par("queueSize");   // in bit
    queueThreshold = par("queueThreshold"); // in bit
    currentQueueSize = 0;
    fifo.setName("FIFO queue");

//
//    // TBF: bucket size
//    const char *bs = par("bucketSize");
//    std::vector<int> v_bs = cStringTokenizer(bs).asIntVector();
//    bucketSize.assign(v_bs.begin(), v_bs.end());
//    std::transform(bucketSize.begin(), bucketSize.end(), bucketSize.begin(), std::bind1st(std::multiplies<long long>(), 8)); // convert bytes to bits
//    if (bucketSize.size() == 1)
//    {
//        int tmp = bucketSize[0];
//        bucketSize.resize(numFlows, tmp);
//    }
//
//    // TBF: mean rate
//    const char *mr = par("meanRate");
//    std::vector<double> v_mr = cStringTokenizer(mr).asDoubleVector();
//    meanRate.assign(v_mr.begin(), v_mr.end());
//    if (meanRate.size() == 1)
//    {
//        int tmp = meanRate[0];
//        meanRate.resize(numFlows, tmp);
//    }
//
//    // TBF: MTU
//    const char *mt = par("mtu");
//    std::vector<int> v_mt = cStringTokenizer(mt).asIntVector();
//    mtu.assign(v_mt.begin(), v_mt.end());
//    std::transform(mtu.begin(), mtu.end(), mtu.begin(), std::bind1st(std::multiplies<long long>(), 8)); // convert bytes to bits
//    if (mtu.size() == 1)
//    {
//        int tmp = mtu[0];
//        mtu.resize(numFlows, tmp);
//    }
//
//    // TBF: peak rate
//    const char *pr = par("peakRate");
//    std::vector<double> v_pr = cStringTokenizer(pr).asDoubleVector();
//    peakRate.assign(v_pr.begin(), v_pr.end());
//    if (peakRate.size() == 1)
//    {
//        int tmp = peakRate[0];
//        peakRate.resize(numFlows, tmp);
//    }
//
//    // TBF: states
//    meanBucketLength = bucketSize;
//    peakBucketLength = mtu;
//    lastTime.assign(numFlows, simTime());

    // CSFQ+TBF: rate estimates
    conformedRate.assign(numFlows, 0.0);
    nonconformedRate.assign(numFlows, 0.0);

    // CSFQ+TBF: flow state
    flowState.resize(numFlows);
    for (int i = 0; i < numFlows; i++) {
        flowState[i].weight_    = 1.0;
        flowState[i].k_         = 100000.0;
        flowState[i].numArv_    = flowState[i].numDpt_ = flowState[i].numDropped_ = 0;
        flowState[i].prevTime_  = 0.0;
        flowState[i].estRate_   = 0.0;
        flowState[i].size_      = 0;
#ifdef SRCID
        hashid_[i]              = 0;
#endif
    }

    // CSFQ
    csfq.alpha_ = csfq.tmpAlpha_ = 0.;
    csfq.kalpha_ = KALPHA;

#ifdef PENALTY_BOX
    for (int i = 0; i < MONITOR_TABLE_SIZE; i++) {
        monitorTable_[i].valid_ = 0;
        monitorTable_[i].prevTime_ = monitorTable_[i].estNormRate_ = 0.;
    }
    for (int i = 0; i < PUNISH_TABLE_SIZE; i++) {
        punishTable_[i].valid_ = 0;
        punishTable_[i].prevTime_ = monitorTable_[i].estNormRate_ = 0.;
    }
    numDroppedPkts_ = 0;
#endif

    csfq.lastArv_ = csfq.rateAlpha_ = csfq.rateTotal_ = 0.;
    csfq.lArv_ = 0.;
    csfq.pktLength_ = csfq.pktLengthE_ = 0;
    csfq.congested_ = 1;

#ifdef PENALTY_BOX
    bind("kLink_", &kDropped_);
#endif

    // statistics
    warmupFinished = false;
    numBitsSent.assign(numFlows, 0.0);
    numPktsReceived.assign(numFlows, 0);
    numPktsDropped.assign(numFlows, 0);
    numPktsConformed.assign(numFlows, 0);
    numPktsSent.assign(numFlows, 0);
}

void CSFQVLANTokenBucketQueue::handleMessage(cMessage *msg)
{
    if (warmupFinished == false)
    {   // start statistics gathering once the warm-up period has passed.
        if (simTime() >= simulation.getWarmupPeriod()) {
            warmupFinished = true;
//            for (int i = 0; i < numFlows; i++)
//            {
//                numPktsReceived[i] = queues[i]->getLength();   // take into account the frames/packets already in queues
//            }
        }
    }

    if (msg->isSelfMessage())
    {   // Something wrong
        error("%s: Received an unexpected self message", getFullPath().c_str());
    }
    else
    {   // a frame arrives
        int flowIndex = classifier->classifyPacket(msg);
        if (warmupFinished == true)
        {
            numPktsReceived[flowIndex]++;
        }
        int pktLength = (check_and_cast<cPacket *>(msg))->getBitLength();
// DEBUG
        ASSERT(pktLength > 0);
// DEBUG
        if (tbm[flowIndex]->meterPacket(msg) == 0)
        {   // frame is conformed
            if (warmupFinished == true)
            {
                numPktsConformed[flowIndex]++;
            }
            // TODO: Refine below
            conformedRate[flowIndex] = estimateRate(flowIndex, pktLength, simTime().dbl());

            if (packetRequested > 0)
            {
                packetRequested--;
                if (warmupFinished == true)
                {
                    numBitsSent[flowIndex] += pktLength;
                    numPktsSent[flowIndex]++;
                }
                sendOut(msg);
            }
            else
            {
                bool dropped = enqueue(msg);
                if (dropped)
                {
                    if (warmupFinished == true)
                    {
                        numPktsDropped[flowIndex]++;
                    }
                }
            }
        }
        else
        {   // frame is not conformed
            conformedRate[flowIndex] = tbm[flowIndex]->getMeanRate(); // TODO: is this right? need skip?
            nonconformedRate[flowIndex] = estimateRate(flowIndex, pktLength, simTime().dbl());  // TODO: refine here

            if (std::max(0.0, 1.0 - csfq.alpha_*tbm[flowIndex]->getMeanRate()/nonconformedRate[flowIndex]) > dblrand())
            {   // drop the frame
                estimateAlpha(pktLength, nonconformedRate[flowIndex], simTime().dbl(), 1);
                delete msg;
                if (warmupFinished == true)
                {
                    numPktsDropped[flowIndex]++;
                }
            }
            else
            {
                estimateAlpha(pktLength, nonconformedRate[flowIndex], simTime().dbl(), 0);
                bool dropped = enqueue(msg);
                if (dropped)
                {
                    if (warmupFinished == true)
                    {
                        numPktsDropped[flowIndex]++;
                    }
                }
            }
        }

        if (ev.isGUI())
        {
            char buf[40];
            sprintf(buf, "q rcvd: %d\nq dropped: %d", numPktsReceived[flowIndex], numPktsDropped[flowIndex]);
            getDisplayString().setTagArg("t", 0, buf);
        }
    }
}

bool CSFQVLANTokenBucketQueue::enqueue(cMessage *msg)
{
    if (queueSize && fifo.length() >= queueSize)
    {
        EV << "FIFO queue full, dropping packet.\n";
        delete msg;
        return true;
    }
    else
    {
        fifo.insert(msg);
        return false;
    }
}

cMessage *CSFQVLANTokenBucketQueue::dequeue()
{
    if (fifo.empty())
    {
        return NULL;
    }
    cMessage *msg = (cMessage *) fifo.pop();
    return msg;
}

void CSFQVLANTokenBucketQueue::sendOut(cMessage *msg)
{
    send(msg, outGate);
}

void CSFQVLANTokenBucketQueue::requestPacket()
{
    Enter_Method("requestPacket()");

    cMessage *msg = dequeue();
    if (msg==NULL)
    {
        packetRequested++;
    }
    else
    {
        if (warmupFinished == true)
        {
            int flowId = classifier->classifyPacket(msg);
            numBitsSent[flowId] += (check_and_cast<cPacket *>(msg))->getBitLength();
            numPktsSent[flowId]++;
        }
        sendOut(msg);
    }
}

// compute estimated flow rate by using exponential averaging
double CSFQVLANTokenBucketQueue::estimateRate(int flowId, int pktLength, double arrvTime)
{
    double d = (arrvTime - flowState[flowId].prevTime_) * 1000000;
    double k = flowState[flowId].k_;

    if (d == 0.0)
    {
        flowState[flowId].size_ += pktLength;
        if (flowState[flowId].estRate_)
        {
            return flowState[flowId].estRate_;
        }
        else
        {   // this is the first packet; just initialize the rate
            return (flowState[flowId].estRate_ = csfq.rateAlpha_ / 2);
        }
    }
    else
    {
        pktLength += flowState[flowId].size_;
        flowState[flowId].size_ = 0;
    }

    flowState[flowId].prevTime_ = arrvTime;
    flowState[flowId].estRate_ = (1. - exp(-d / k)) * (double) pktLength / d + exp(-d / k) * flowState[flowId].estRate_;
    return flowState[flowId].estRate_;
}

// estimate the link's alpha parameter
void CSFQVLANTokenBucketQueue::estimateAlpha(int pktLength, double arrvRate, double arrvTime, int enqueue)
{
    float d = (arrvTime - csfq.lastArv_) * 1000000., w, rate = csfq.rate_ / 1000000.;
    double k = csfq.kLink_ / 1000000.;

    // set lastArv_ to the arrival time of the first packet
    if (csfq.lastArv_ == 0.)
    {
        csfq.lastArv_ = arrvTime;
    }

    // account for packets received simultaneously
    csfq.pktLength_ += pktLength;
    if (enqueue)
    {
        csfq.pktLengthE_ += pktLength;
    }
    if (arrvTime == csfq.lastArv_)
    {
        return;
    }

    // estimate the aggregate arrival rate (rateTotal_) and the
    // aggregate forwarded (rateAlpha_) rates
    w = exp(-d / csfq.kLink_);
    csfq.rateAlpha_ = (1 - w) * (double) csfq.pktLengthE_ / d + w * csfq.rateAlpha_;
    csfq.rateTotal_ = (1 - w) * (double) csfq.pktLength_ / d + w * csfq.rateTotal_;
    csfq.lastArv_ = arrvTime;
    csfq.pktLength_ = csfq.pktLengthE_ = 0;

    // compute the initial value of alpha_
    if (csfq.alpha_ == 0.)
    {
        if (currentQueueSize < queueThreshold)
        {
            if (csfq.tmpAlpha_ < arrvRate)
            {
                csfq.tmpAlpha_ = arrvRate;
            }
            return;
        }
        if (csfq.alpha_ < csfq.tmpAlpha_)
        {
            csfq.alpha_ = csfq.tmpAlpha_;
        }
        if (csfq.alpha_ == 0.)
        {
            csfq.alpha_ = rate / 2.;  // arbitrary initialization
        }
        csfq.tmpAlpha_ = 0.;
    }
    // update alpha_
    if (rate <= csfq.rateTotal_)
    {   // link congested
        if (!csfq.congested_)
        {
            csfq.congested_ = 1;
            csfq.lArv_ = arrvTime;
            csfq.kalpha_ = KALPHA;
        }
        else
        {
            if (arrvTime < csfq.lArv_ + k)
            {
                return;
            }
            csfq.lArv_ = arrvTime;
            csfq.alpha_ *= rate / csfq.rateAlpha_;
            if (rate < csfq.alpha_)
            {
                csfq.alpha_ = rate;
            }
        }
    }
    else
    {   // (rate < rateTotal_) => link uncongested
        if (csfq.congested_)
        {
            csfq.congested_ = 0;
            csfq.lArv_ = arrvTime;
            csfq.tmpAlpha_ = 0;
        }
        else
        {
            if (arrvTime < csfq.lArv_ + k)
            {
                if (csfq.tmpAlpha_ < arrvRate)
                {
                    csfq.tmpAlpha_ = arrvRate;
                }
            }
            else
            {
                csfq.alpha_ = csfq.tmpAlpha_;
                csfq.lArv_ = arrvTime;
                if (currentQueueSize < queueThreshold)
                {
                    csfq.alpha_ = 0.;
                }
                else
                {
                    csfq.tmpAlpha_ = 0.;
                }
            }
        }
    }
#ifdef CSFQ_LOG
    EV << id_ << " " << arrvTime << " " << csfq.rateAlpha_ << " " << csfq.rateTotal_;
#endif
}

void CSFQVLANTokenBucketQueue::finish()
{
    unsigned long sumPktsReceived = 0;
    unsigned long sumPktsDropped = 0;
    unsigned long sumPktsShaped = 0;

    for (int i=0; i < numFlows; i++)
    {
        std::stringstream ss_received, ss_dropped, ss_shaped, ss_sent, ss_throughput;
        ss_received << "packets received from flow[" << i << "]";
        ss_dropped << "packets dropped from flow[" << i << "]";
        ss_shaped << "packets shaped from flow[" << i << "]";
        ss_sent << "packets sent from flow[" << i << "]";
        ss_throughput << "bits/sec sent from flow[" << i << "]";
        recordScalar((ss_received.str()).c_str(), numPktsReceived[i]);
        recordScalar((ss_dropped.str()).c_str(), numPktsDropped[i]);
        recordScalar((ss_shaped.str()).c_str(), numPktsReceived[i]-numPktsConformed[i]);
        recordScalar((ss_sent.str()).c_str(), numPktsSent[i]);
        recordScalar((ss_throughput.str()).c_str(), numBitsSent[i]/(simTime()-simulation.getWarmupPeriod()).dbl());
        sumPktsReceived += numPktsReceived[i];
        sumPktsDropped += numPktsDropped[i];
        sumPktsShaped += numPktsReceived[i] - numPktsConformed[i];
    }
    recordScalar("overall packet loss rate", sumPktsDropped/double(sumPktsReceived));
    recordScalar("overall packet shaped rate", sumPktsShaped/double(sumPktsReceived));
}

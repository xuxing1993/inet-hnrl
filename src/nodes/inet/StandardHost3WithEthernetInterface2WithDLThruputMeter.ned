//
// Copyright (C) 2013 Kyeong Soo (Joseph) Kim
// Copyright (C) 2004 Andras Varga
// Copyright (C) 2000 Institut fuer Telematik, Universitaet Karlsruhe
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//


package inet.nodes.inet;

import inet.applications.pingapp.PingApp;
import inet.applications.sctpapp.SCTPApp;
import inet.applications.tcpapp.TCPApp;
import inet.applications.tcpapp.TCPApp2;
//import inet.applications.tcpapp.HttpClientApp;
import inet.applications.udpapp.UDPApp;
//import inet.applications.udpapp.UDPVideoStreamCli;
//import inet.applications.udpapp.VideoApp;
import inet.base.NotificationBoard;
import inet.linklayer.ethernet.EthernetInterface2WithDLThruputMeter;
import inet.linklayer.ext.ExtInterface;
import inet.linklayer.ppp.PPPInterface;
import inet.networklayer.common.InterfaceTable;
import inet.networklayer.ipv4.RoutingTable;
import inet.transport.sctp.SCTP;
import inet.transport.tcp.ITCP;
import inet.transport.udp.UDP;
import inet.util.NAMTraceWriter;
import inet.util.TCPDump;


//
// \IP host with TCP, UDP layers and applications AND EthernetInterface2WithDLThruputMeter.
// Note that ping application has been removed from the original StandardHost module.
//
module StandardHost3WithEthernetInterface2WithDLThruputMeter
{
    parameters:
        @node();
        int numTcpApps = default(0);
        int numFtpApps = default(0);
        int numHttpApps = default(0);
        int numUdpApps = default(0);
        int numVideoApps = default(0);
        int numSctpApps = default(0);
        string tcpAppType = default("n/a");
        string ftpAppType = default("n/a");
        string httpAppType = default("n/a");
        string udpAppType = default("n/a");
        string videoAppType = default("n/a");
        string sctpAppType = default("n/a");
        string tcpType = default("TCP");  // tcp implementation (e.g. TCP, TCP_old, TCP_NSC)
        bool IPForward = default(false);
        int namid = default(-1);
        string routingFile = default("");
        int numExtInterfaces = default(0);
        @display("i=device/pc2;bgb=760,453");
    gates:
        inout pppg[];
        inout ethg[];
    submodules:
        namTrace: NAMTraceWriter {
            parameters:
                namid = namid;
                @display("p=57,294");
        }
        notificationBoard: NotificationBoard {
            parameters:
                @display("p=57,54");
        }
        interfaceTable: InterfaceTable {
            parameters:
                @display("p=57,134");
        }
        routingTable: RoutingTable {
            parameters:
                IPForward = IPForward;
                routerId = "";
                routingFile = routingFile;
                @display("p=57,214");
        }
        tcpApp[numTcpApps]: <tcpAppType> like TCPApp {
            parameters:
                @display("p=292,54");
        }
        ftpApp[numFtpApps]: <ftpAppType> like TCPApp2 {
            parameters:
                port = index+1024;
                @display("p=169,54");
        }
        httpApp[numHttpApps]: <httpAppType> like TCPApp2 {
            parameters:
                port = index+2048; // TODO: Remove the limit in the max. number of ftpApp's (i.e., 1024)
                @display("p=169,140");
        }
        tcp: <tcpType> like ITCP {
            parameters:
                @display("p=292,140");
        }
        udpApp[numUdpApps]: <udpAppType> like UDPApp {
            parameters:
                @display("p=537,54");
        }
        videoApp[numVideoApps]: <videoAppType> like UDPApp {
            parameters:
                @display("p=412,54");
        }
        udp: UDP {
            parameters:
                @display("p=476,140");
        }
        sctpApp[numSctpApps]: <sctpAppType> like SCTPApp {
            parameters:
                @display("p=681,54");
        }
        sctp: SCTP {
            @display("p=681,140;i=block/wheelbarrow");
        }
        networkLayer: NetworkLayer {
            parameters:
                proxyARP = false;
                @display("p=476,229;q=queue");
            gates:
                ifIn[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
                ifOut[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
        }
        ppp[sizeof(pppg)]: PPPInterface {
            parameters:
                @display("p=320,389,row,90;q=txQueue");
        }
        eth[sizeof(ethg)]: EthernetInterface2WithDLThruputMeter {
            parameters:
                @display("p=476,389,row,90;q=txQueue");
        }
        ext[numExtInterfaces]: ExtInterface {
            parameters:
                @display("p=635,389,row,90;q=txQueue;i=block/ifcard");
        }
        tcpdump: TCPDump {
            parameters:
                @display("p=476,301;i=abstract/cache_s");
            gates:
                ifIn[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
                in2[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
                ifOut[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
                out2[sizeof(pppg)+sizeof(ethg)+numExtInterfaces];
        }
    connections allowunconnected:
        for i=0..numTcpApps-1 {
            tcpApp[i].tcpOut --> tcp.appIn++;
            tcpApp[i].tcpIn <-- tcp.appOut++;
        }

        for i=0..numFtpApps-1 {
            ftpApp[i].tcpOut --> tcp.appIn++;
            ftpApp[i].tcpIn <-- tcp.appOut++;
        }

        for i=0..numHttpApps-1 {
            httpApp[i].tcpOut --> tcp.appIn++;
            httpApp[i].tcpIn <-- tcp.appOut++;
        }

        tcp.ipOut --> networkLayer.tcpIn;
        tcp.ipIn <-- networkLayer.tcpOut;

        for i=0..numUdpApps-1 {
            udpApp[i].udpOut --> udp.appIn++;
            udpApp[i].udpIn <-- udp.appOut++;
        }

        for i=0..numVideoApps-1 {
            videoApp[i].udpOut --> udp.appIn++;
            videoApp[i].udpIn <-- udp.appOut++;
        }

        udp.ipOut --> networkLayer.udpIn;
        udp.ipIn <-- networkLayer.udpOut;

        for i=0..numSctpApps-1 {
            sctpApp[i].sctpOut --> sctp.from_appl++;
            sctp.to_appl++ --> sctpApp[i].sctpIn;
        }
        sctp.to_ip --> networkLayer.sctpIn;
        networkLayer.sctpOut --> sctp.from_ip;

        // connections to network outside
        for i=0..sizeof(pppg)-1 {
            pppg[i] <--> ppp[i].phys;
            ppp[i].netwOut --> tcpdump.ifIn[i]; //I.R.
            tcpdump.out2[i] --> networkLayer.ifIn[i];
            ppp[i].netwIn <-- tcpdump.ifOut[i];
            tcpdump.in2[i] <-- networkLayer.ifOut[i];
        }

        for i=0..sizeof(ethg)-1 {
            ethg[i] <--> eth[i].phys;
            eth[i].netwOut --> tcpdump.ifIn[sizeof(pppg)+i]; //I.R.
            tcpdump.out2[sizeof(pppg)+i] --> networkLayer.ifIn[sizeof(pppg)+i];
            eth[i].netwIn <-- tcpdump.ifOut[sizeof(pppg)+i];
            tcpdump.in2[sizeof(pppg)+i] <-- networkLayer.ifOut[sizeof(pppg)+i];
        }

        for i=0..numExtInterfaces-1 {
            ext[i].netwOut --> tcpdump.ifIn[sizeof(pppg)+sizeof(ethg)+i];
            tcpdump.out2[sizeof(pppg)+sizeof(ethg)+i] --> networkLayer.ifIn[sizeof(pppg)+sizeof(ethg)+i];
            ext[i].netwIn <-- tcpdump.ifOut[sizeof(pppg)+sizeof(ethg)+i];
            tcpdump.in2[sizeof(pppg)+sizeof(ethg)+i] <-- networkLayer.ifOut[sizeof(pppg)+sizeof(ethg)+i];
        }
}


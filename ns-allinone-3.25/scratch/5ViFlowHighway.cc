/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/netanim-module.h"		// NetAnim module 
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/type-id.h"
#include "ns3/object-base.h"
#include "ns3/scheduler.h"
#include "ns3/calendar-scheduler.h"
#include "ns3/callback.h"
#include <iostream>
//#include <sstream>
//#include <string>
#include "ns3/evalvid-client-server-helper.h"
#include "ns3/evalvid-server.h"
//#include "ns3/gtk-config-store.h"	// IPython

#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ThirdScriptExample");

// Check the tags of packets received
void ReceivePacket (Ptr<const Packet> pkt, const Address &){
  QosTag q_tag;
  if (pkt->PeekPacketTag(q_tag))
  {
	  //NS_LOG_UNCOND ("Received Packet Tag: " << (int)q_tag.GetTid() << "\n");
  }
}

// Set qos-tag for packets
void SetTagTid (uint8_t mytid, Ptr<const Packet> pkt ){
  QosTag  qosTag ;
  qosTag.SetTid (mytid);
  pkt->AddPacketTag (qosTag);
  //NS_LOG_UNCOND ("Sent Packet with Tag: " << (int)mytid << "\n");
}

int 
main (int argc, char *argv[])
{  
  //Enable logging for EvalvidClient and Evalvid Server
  LogComponentEnable ("EvalvidClient", LOG_LEVEL_INFO);
  LogComponentEnable ("EvalvidServer", LOG_LEVEL_INFO);
  
  //Enable logging for Dca and Edca
  //LogComponentEnable ("DcaTxop", LOG_LEVEL_ALL);
  //LogComponentEnable ("EdcaTxopN", LOG_FUNCTION);
	
  bool verbose = true;
  uint32_t nWifi = 4;
  std::string rtslimit = "3000";
  uint32_t qcase = 1;						//Queue selection
  uint32_t selectapp = -1;						//Queue selection
  uint32_t maxpktnum = 400;					//This is the default value for maximum packet number in a queue
  //std::string stfile = "st_rush_hour_1080.st";	//video to use
  std::string stfile = "st_highway_cif.st";	//video to use
  
  /*uint32_t Vi1Time = 0;
  uint32_t Vi2Time = 5;
  uint32_t Vi3Time = 10;
  uint32_t Vi4Time = 15;
  uint32_t Vi5Time = 20;
  */
  uint32_t psnrthreshold = 30;
 
  
  uint32_t Vi1Time = (rand() % 10) + 1;
  uint32_t Vi2Time = (rand() % 10) + 1;
  uint32_t Vi3Time = (rand() % 10) + 1;
  uint32_t Vi4Time = (rand() % 10) + 1;
  uint32_t Vi5Time = (rand() % 10) + 1;

  CommandLine cmd;
  cmd.AddValue ("nWifi", "Number of wifi STA devices", nWifi);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("stfile", "Tell echo applications to log if true", stfile);
  cmd.AddValue ("maxpktnum", "maximum number of packet in queue", maxpktnum);
  cmd.AddValue ("qcase", "which algorithm enqueue to use in sta-wifi-mac", qcase);
  cmd.AddValue ("selectapp", "which algorithm enqueue to use in sta-wifi-mac", selectapp);	//select individual app that has the proposed QoE ability
  cmd.AddValue ("Vi1Time", "additional start/ stop time", Vi1Time);
  cmd.AddValue ("Vi2Time", "additional start/ stop time", Vi2Time);
  cmd.AddValue ("Vi3Time", "additional start/ stop time", Vi3Time);
  cmd.AddValue ("Vi4Time", "additional start/ stop time", Vi4Time);
  cmd.AddValue ("Vi5Time", "additional start/ stop time", Vi5Time);
  cmd.AddValue ("psnrthreshold", "change the psnr threhold", psnrthreshold);
    
  cmd.Parse (argc,argv);

  if (nWifi > 18) {
      std::cout << "Number of wifi nodes " << nWifi << 
                   " specified exceeds the mobility bounding box" << std::endl;
      exit (1);
  }

  if (verbose) {
      LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
  }
    
  // Turn off RTS/CTS below 3000 bytes
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue (rtslimit));
  
  NodeContainer wifiApNode;			//Create AP nodes (Node0) wifiApNode1
  wifiApNode.Create(1);
   
  NodeContainer wifiStaNodes;		//Create wifi nodes (n1, n2, n3 and n4) wifiStaNodes0, wifiStaNodes1, wifiStaNodes2, wifiStaNodes3
  wifiStaNodes.Create (nWifi);
  
  // Create & setup wifi channel
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
  
  phy.SetChannel (channel.Create ());
  
  // Install wireless devices
  WifiHelper wifi = WifiHelper::Default ();
  wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  QosWifiMacHelper mac = QosWifiMacHelper::Default ();

  Ssid ssid = Ssid ("ns-3-ssid");
   
  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "qcase", UintegerValue(qcase),
               "psnrthreshold", UintegerValue(psnrthreshold),
               "selectapp", UintegerValue(selectapp),
               "ActiveProbing", BooleanValue (false),
               "QosSupported", BooleanValue(true));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, mac, wifiStaNodes);
  
  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid),
               "QosSupported", BooleanValue(true));  


  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, mac, wifiApNode);
  
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject <ListPositionAllocator>();
  positionAlloc ->Add(Vector(30, 20, 0)); // n0 (AP)
  positionAlloc ->Add(Vector(10, 10, 0)); // n1
  positionAlloc ->Add(Vector(40, 40, 0)); // n2
  positionAlloc ->Add(Vector(10, 40, 0)); // n3
  positionAlloc ->Add(Vector(50, 10, 0)); // n4
  mobility.SetPositionAllocator(positionAlloc);
  
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);
  
  mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                             "Bounds", RectangleValue (Rectangle (-50, 50, -50, 50)));
                             
  mobility.Install(wifiStaNodes);  
  
  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNodes);

  Ipv4AddressHelper address;

  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer wifiInterfaces, wifiApInterface;
  wifiApInterface = address.Assign (apDevices);	// 10.1.3.1 / 24
  wifiInterfaces = address.Assign (staDevices); // 10.1.3.x / 24
  
  // Set Max Packet Number in Queue
  // ==============================
  // Note:  NodeList 0 is the AP
  Config::Set("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/BK_EdcaTxopN/Queue/MaxPacketNumber", UintegerValue(maxpktnum));
  Config::Set("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/BE_EdcaTxopN/Queue/MaxPacketNumber", UintegerValue(maxpktnum));
  Config::Set("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/VI_EdcaTxopN/Queue/MaxPacketNumber", UintegerValue(maxpktnum));
  Config::Set("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/VO_EdcaTxopN/Queue/MaxPacketNumber", UintegerValue(maxpktnum));
     
  uint16_t port = 4009;		//port number for servers
  //int N1AppList = -1;		//ApplicationList for N1
  std::string N1AppListStr = "";
    
  // PAIR 1
  // =======
  // VI connection from n1 to n2
  port+=1;
  
  // EvalvidClient (RECEIVER)
  // ************************
   
  EvalvidClientHelper vi1_rx (wifiInterfaces.GetAddress(0),port);			//insert sender's address, sender's port# in the bracket
  vi1_rx.SetAttribute ("ReceiverDumpFilename", StringValue("rd_a01_vi1_rx_highway"));
  ApplicationContainer vi1_rx_app;
  vi1_rx_app = vi1_rx.Install (wifiStaNodes.Get(1));						//install rx app on the receiver's node
  
  vi1_rx_app.Start (Seconds (10.0 + Vi1Time));
  vi1_rx_app.Stop (Seconds (100.0 + Vi1Time));
  
  // EvalvidServer (SENDER)
  // **********************
  EvalvidServerHelper vi1_tx (port);
  vi1_tx.SetAttribute ("SenderTraceFilename", StringValue(stfile));
  vi1_tx.SetAttribute ("SenderDumpFilename", StringValue("sd_a01_vi1_tx_highway"));
  vi1_tx.SetAttribute ("PacketPayload", UintegerValue(1014));
  
  ApplicationContainer vi1_tx_app;
  vi1_tx_app = vi1_tx.Install (wifiStaNodes.Get(0));
  
  vi1_tx_app.Start (Seconds (5.0 + Vi1Time));  //application START
  vi1_tx_app.Stop (Seconds (200.0 + Vi1Time));  //application STOP
  // -------------------------------------------------------------------
  
  // PAIR 2
  // =======
  // VI connection from n1 to n3
  port+=1;
  
  // EvalvidClient (RECEIVER)
  // ************************
 
  EvalvidClientHelper vi2_rx (wifiInterfaces.GetAddress(0),port);
  vi2_rx.SetAttribute ("ReceiverDumpFilename", StringValue("rd_a01_vi2_rx_highway"));
  ApplicationContainer vi2_rx_app;
  vi2_rx_app = vi2_rx.Install (wifiStaNodes.Get(2));
  
  vi2_rx_app.Start (Seconds (10.0 + Vi2Time));
  vi2_rx_app.Stop (Seconds (100.0 + Vi2Time));
  
  // EvalvidServer (SENDER)
  // **********************
  EvalvidServerHelper vi2_tx (port);
  vi2_tx.SetAttribute ("SenderTraceFilename", StringValue(stfile));
  vi2_tx.SetAttribute ("SenderDumpFilename", StringValue("sd_a01_vi2_tx_highway"));
  vi2_tx.SetAttribute ("PacketPayload", UintegerValue(1014));
   
  ApplicationContainer vi2_tx_app;
  vi2_tx_app = vi2_tx.Install (wifiStaNodes.Get(0));
   
  vi2_tx_app.Start (Seconds (5.0 + Vi2Time));  //application START
  vi2_tx_app.Stop (Seconds (200.0 + Vi2Time));  //application STOP
      
  //*******//
  
  // PAIR 3
  // =======
  // VI connection from n1 to n4
  port+=1;
  
  // EvalvidClient (RECEIVER)
  // ************************
 
  EvalvidClientHelper vi3_rx (wifiInterfaces.GetAddress(0),port);
  vi3_rx.SetAttribute ("ReceiverDumpFilename", StringValue("rd_a01_vi3_rx_highway"));
  ApplicationContainer vi3_rx_app;
  vi3_rx_app = vi3_rx.Install (wifiStaNodes.Get(3));
  
  vi3_rx_app.Start (Seconds (10.0 + Vi3Time));
  vi3_rx_app.Stop (Seconds (100.0 + Vi3Time));
  
  // EvalvidServer (SENDER)
  // **********************
  EvalvidServerHelper vi3_tx (port);
  vi3_tx.SetAttribute ("SenderTraceFilename", StringValue(stfile));
  vi3_tx.SetAttribute ("SenderDumpFilename", StringValue("sd_a01_vi3_tx_highway"));
  vi3_tx.SetAttribute ("PacketPayload", UintegerValue(1014));
   
  ApplicationContainer vi3_tx_app;
  vi3_tx_app = vi3_tx.Install (wifiStaNodes.Get(0));
   
  vi3_tx_app.Start (Seconds (5.0 + Vi3Time));  //application START
  vi3_tx_app.Stop (Seconds (200.0 + Vi3Time));  //application STOP
      
  //*******//
  
  // PAIR 4
  // =======
  // VI connection from n1 to n2
  port+=1;
  
  // EvalvidClient (RECEIVER)
  // ************************
   
  EvalvidClientHelper vi4_rx (wifiInterfaces.GetAddress(0),port);			//insert sender's address, sender's port# in the bracket
  vi4_rx.SetAttribute ("ReceiverDumpFilename", StringValue("rd_a01_vi4_rx_highway"));
  ApplicationContainer vi4_rx_app;
  vi4_rx_app = vi4_rx.Install (wifiStaNodes.Get(1));						//install rx app on the receiver's node
  
  vi4_rx_app.Start (Seconds (10.0 + Vi4Time));
  vi4_rx_app.Stop (Seconds (100.0 + Vi4Time));
  
  // EvalvidServer (SENDER)
  // **********************
  EvalvidServerHelper vi4_tx (port);
  vi4_tx.SetAttribute ("SenderTraceFilename", StringValue(stfile));
  vi4_tx.SetAttribute ("SenderDumpFilename", StringValue("sd_a01_vi4_tx_highway"));
  vi4_tx.SetAttribute ("PacketPayload", UintegerValue(1014));
  
  ApplicationContainer vi4_tx_app;
  vi4_tx_app = vi4_tx.Install (wifiStaNodes.Get(0));
  
  vi4_tx_app.Start (Seconds (5.0 + Vi4Time));  //application START
  vi4_tx_app.Stop (Seconds (200.0 + Vi4Time));  //application STOP
      
  //*******//
  
  // PAIR 5
  // =======
  // VI connection from n1 to n3
  port+=1;
  
  // EvalvidClient (RECEIVER)
  // ************************
 
  EvalvidClientHelper vi5_rx (wifiInterfaces.GetAddress(0),port);
  vi5_rx.SetAttribute ("ReceiverDumpFilename", StringValue("rd_a01_vi5_rx_highway"));
  ApplicationContainer vi5_rx_app;
  vi5_rx_app = vi5_rx.Install (wifiStaNodes.Get(2));
  
  vi5_rx_app.Start (Seconds (10.0 + Vi5Time));
  vi5_rx_app.Stop (Seconds (100.0 + Vi5Time));
  
  // EvalvidServer (SENDER)
  // **********************
  EvalvidServerHelper vi5_tx (port);
  vi5_tx.SetAttribute ("SenderTraceFilename", StringValue(stfile));
  vi5_tx.SetAttribute ("SenderDumpFilename", StringValue("sd_a01_vi5_tx_highway"));
  vi5_tx.SetAttribute ("PacketPayload", UintegerValue(1014));
   
  ApplicationContainer vi5_tx_app;
  vi5_tx_app = vi5_tx.Install (wifiStaNodes.Get(0));
   
  vi5_tx_app.Start (Seconds (5.0 + Vi5Time));  //application START
  vi5_tx_app.Stop (Seconds (200.0 + Vi5Time));  //application STOP
      
  //*******//
    
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  
  Simulator::Stop (Seconds (500.0));
  
  phy.EnablePcap ("evalvid_container", apDevices);
  phy.EnablePcap ("evalvid_container" , staDevices);
  phy.EnableAscii ("evalvid_container" , apDevices);
  phy.EnableAscii ("evalvid_container" , staDevices);
  
  AnimationInterface anim ("simulationScenario_5ViFlow.xml");
  anim.SetMaxPktsPerTraceFile(500000); //Increase Max Packets Per File to prevent "Max Packets per Trace File Exceeded" error.
   
  NS_LOG_INFO ("Run Simulation.");
  
  //GtkConfigStore config;			// IPython
  //config.ConfigureAttributes();	// IPython
  
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
  
  Simulator::Run ();
  
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
  {
	Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

 	if (t.sourceAddress == "10.1.3.2")
 	{
	 	NS_LOG_UNCOND("Flow ID: 	" 	<< iter->first );
	 	NS_LOG_UNCOND("Src Addr: 	" 	<< t.sourceAddress << "\tDst Addr: " << t.destinationAddress << "\tSrc Port: " << t.sourcePort << "\tDst Port: " << t.destinationPort);
		NS_LOG_UNCOND("Tx Packets = " 	<< iter->second.txPackets);
		NS_LOG_UNCOND("Rx Packets = " 	<< iter->second.rxPackets);
		NS_LOG_UNCOND("Lost Packets = " << iter->second.lostPackets);
		NS_LOG_UNCOND("Throughput: 	" 	<< iter->second.rxBytes * 8.0 / (iter->second.timeLastRxPacket.GetSeconds()
										-iter->second.timeFirstTxPacket.GetSeconds()) / 1024  << " Kbps\n\n");
	}
  }
  
  monitor->SerializeToXmlFile("simulationScenario_v4.xml", true, true);
   
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

}

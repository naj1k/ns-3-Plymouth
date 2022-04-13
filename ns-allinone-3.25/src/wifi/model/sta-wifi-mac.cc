/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
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
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#include "sta-wifi-mac.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/boolean.h"
#include "ns3/trace-source-accessor.h"
#include "qos-tag.h"
#include "mac-low.h"
#include "dcf-manager.h"
#include "mac-rx-middle.h"
#include "mac-tx-middle.h"
#include "wifi-mac-header.h"
#include "msdu-aggregator.h"
#include "amsdu-subframe-header.h"
#include "mgt-headers.h"
#include "ht-capabilities.h"
#include "ht-operations.h"
#include "vht-capabilities.h"

//addd by Najwan
#include "ns3/node-id-tag.h"	//needed for identifying node id tag in CheckPacketInfo()
#include "ns3/frame-type-tag.h" //needed for identifying frame type tag in CheckPacketInfo()
#include "ns3/app-id-tag.h"		//needed for identifying application id tag in CheckPacketInfo()
#include "ns3/dev-id-tag.h"		//needed for identifying device id tag in CheckPacketInfo()
#include "ns3/ipv4-header.h"	//needed for identifying source and destination IP in CheckPacketInfo()
#include "ns3/seq-ts-header.h"  //needed for identifying packet sequence in CheckPacketInfo()
#include "ns3/udp-header.h"		//needed for identifying source port in CheckPacketInfo()
#include "ns3/evalvid-server.h" //to use in conjunction with EvalvidServer pointer
#include "ns3/node-list.h"		//to use in conjuction with the NodeList
#include "wifi-net-device.h"	//to use in conjuction with WifiNetDevice pointer line 494
#include "wifi-mac-queue.h"		//to use in conjuction with accessing the WifiMacQueue pointer and the BK,BE,VI,VO Accessor
#include <fstream> 				//To use with std::ofstream
#include <iostream>
#include <string>

/*
 * The state machine for this STA is:
 --------------                                          -----------
 | Associated |   <--------------------      ------->    | Refused |
 --------------                        \    /            -----------
    \                                   \  /
     \    -----------------     -----------------------------
      \-> | Beacon Missed | --> | Wait Association Response |
          -----------------     -----------------------------
                \                       ^
                 \                      |
                  \    -----------------------
                   \-> | Wait Probe Response |
                       -----------------------
 */

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("StaWifiMac");

NS_OBJECT_ENSURE_REGISTERED (StaWifiMac);

TypeId
StaWifiMac::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::StaWifiMac")
    .SetParent<RegularWifiMac> ()
    .SetGroupName ("Wifi")
    .AddConstructor<StaWifiMac> ()
    .AddAttribute ("ProbeRequestTimeout", "The interval between two consecutive probe request attempts.",
                   TimeValue (Seconds (0.05)),
                   MakeTimeAccessor (&StaWifiMac::m_probeRequestTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("AssocRequestTimeout", "The interval between two consecutive assoc request attempts.",
                   TimeValue (Seconds (0.5)),
                   MakeTimeAccessor (&StaWifiMac::m_assocRequestTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("MaxMissedBeacons",
                   "Number of beacons which much be consecutively missed before "
                   "we attempt to restart association.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&StaWifiMac::m_maxMissedBeacons),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("qcase",
                   "Choose which algorithm to use "
                   "for enqueueing.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&StaWifiMac::m_qcase),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("selectapp",
                   "Choose which app "
                   "has the proposed algorithm.",
                   UintegerValue (-1),
                   MakeUintegerAccessor (&StaWifiMac::m_selectapp),
                   MakeUintegerChecker<uint32_t> ())             
    .AddAttribute ("psnrthreshold",
                   "Choose the value of "
                   "psnr threshold.",
                   UintegerValue (30),
                   MakeUintegerAccessor (&StaWifiMac::m_psnrthreshold),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ActiveProbing",
                   "If true, we send probe requests. If false, we don't."
                   "NOTE: if more than one STA in your simulation is using active probing, "
                   "you should enable it at a different simulation time for each STA, "
                   "otherwise all the STAs will start sending probes at the same time resulting in collisions. "
                   "See bug 1060 for more info.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&StaWifiMac::SetActiveProbing, &StaWifiMac::GetActiveProbing),
                   MakeBooleanChecker ())
    .AddTraceSource ("Assoc", "Associated with an access point.",
                     MakeTraceSourceAccessor (&StaWifiMac::m_assocLogger),
                     "ns3::Mac48Address::TracedCallback")
    .AddTraceSource ("DeAssoc", "Association with an access point lost.",
                     MakeTraceSourceAccessor (&StaWifiMac::m_deAssocLogger),
                     "ns3::Mac48Address::TracedCallback")
  ;
  return tid;
}

StaWifiMac::StaWifiMac ()
  : m_state (BEACON_MISSED),
    m_probeRequestEvent (),
    m_assocRequestEvent (),
    m_beaconWatchdogEnd (Seconds (0.0)),
    //To be used in Case 8's Setter and Getter
    m_IDrop(0),
    m_PDrop(0),
    m_BDrop(0)
{
  NS_LOG_FUNCTION (this);

  //Let the lower layers know that we are acting as a non-AP STA in
  //an infrastructure BSS.
  SetTypeOfStation (STA);
}

StaWifiMac::~StaWifiMac ()
{
  NS_LOG_FUNCTION (this);
}

void
StaWifiMac::SetMaxMissedBeacons (uint32_t missed)
{
  NS_LOG_FUNCTION (this << missed);
  m_maxMissedBeacons = missed;
}

void
StaWifiMac::SetProbeRequestTimeout (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_probeRequestTimeout = timeout;
}

void
StaWifiMac::SetAssocRequestTimeout (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_assocRequestTimeout = timeout;
}

void
StaWifiMac::StartActiveAssociation (void)
{
  NS_LOG_FUNCTION (this);
  TryToEnsureAssociated ();
}

void
StaWifiMac::SetActiveProbing (bool enable)
{
  NS_LOG_FUNCTION (this << enable);
  if (enable)
    {
      Simulator::ScheduleNow (&StaWifiMac::TryToEnsureAssociated, this);
    }
  else
    {
      m_probeRequestEvent.Cancel ();
    }
  m_activeProbing = enable;
}

bool StaWifiMac::GetActiveProbing (void) const
{
  return m_activeProbing;
}

void
StaWifiMac::SendProbeRequest (void)
{
  NS_LOG_FUNCTION (this);
  WifiMacHeader hdr;
  hdr.SetProbeReq ();
  hdr.SetAddr1 (Mac48Address::GetBroadcast ());
  hdr.SetAddr2 (GetAddress ());
  hdr.SetAddr3 (Mac48Address::GetBroadcast ());
  hdr.SetDsNotFrom ();
  hdr.SetDsNotTo ();
  Ptr<Packet> packet = Create<Packet> ();
  MgtProbeRequestHeader probe;
  probe.SetSsid (GetSsid ());
  probe.SetSupportedRates (GetSupportedRates ());
  if (m_htSupported || m_vhtSupported)
    {
      probe.SetHtCapabilities (GetHtCapabilities ());
      hdr.SetNoOrder ();
    }
  if (m_vhtSupported)
    {
      probe.SetVhtCapabilities (GetVhtCapabilities ());
    }
  packet->AddHeader (probe);

  //The standard is not clear on the correct queue for management
  //frames if we are a QoS AP. The approach taken here is to always
  //use the DCF for these regardless of whether we have a QoS
  //association or not.
  m_dca->Queue (packet, hdr);

  if (m_probeRequestEvent.IsRunning ())
    {
      m_probeRequestEvent.Cancel ();
    }
  m_probeRequestEvent = Simulator::Schedule (m_probeRequestTimeout,
                                             &StaWifiMac::ProbeRequestTimeout, this);
}

void
StaWifiMac::SendAssociationRequest (void)
{
  NS_LOG_FUNCTION (this << GetBssid ());
  WifiMacHeader hdr;
  hdr.SetAssocReq ();
  hdr.SetAddr1 (GetBssid ());
  hdr.SetAddr2 (GetAddress ());
  hdr.SetAddr3 (GetBssid ());
  hdr.SetDsNotFrom ();
  hdr.SetDsNotTo ();
  Ptr<Packet> packet = Create<Packet> ();
  MgtAssocRequestHeader assoc;
  assoc.SetSsid (GetSsid ());
  assoc.SetSupportedRates (GetSupportedRates ());
  assoc.SetCapabilities (GetCapabilities ());
  if (m_htSupported || m_vhtSupported)
    {
      assoc.SetHtCapabilities (GetHtCapabilities ());
      hdr.SetNoOrder ();
    }
  if (m_vhtSupported)
    {
      assoc.SetVhtCapabilities (GetVhtCapabilities ());
    }
  packet->AddHeader (assoc);

  //The standard is not clear on the correct queue for management
  //frames if we are a QoS AP. The approach taken here is to always
  //use the DCF for these regardless of whether we have a QoS
  //association or not.
  m_dca->Queue (packet, hdr);

  if (m_assocRequestEvent.IsRunning ())
    {
      m_assocRequestEvent.Cancel ();
    }
  m_assocRequestEvent = Simulator::Schedule (m_assocRequestTimeout,
                                             &StaWifiMac::AssocRequestTimeout, this);
}

void
StaWifiMac::TryToEnsureAssociated (void)
{
  NS_LOG_FUNCTION (this);
  switch (m_state)
    {
    case ASSOCIATED:
      return;
      break;
    case WAIT_PROBE_RESP:
      /* we have sent a probe request earlier so we
         do not need to re-send a probe request immediately.
         We just need to wait until probe-request-timeout
         or until we get a probe response
       */
      break;
    case BEACON_MISSED:
      /* we were associated but we missed a bunch of beacons
       * so we should assume we are not associated anymore.
       * We try to initiate a probe request now.
       */
      m_linkDown ();
      if (m_activeProbing)
        {
          SetState (WAIT_PROBE_RESP);
          SendProbeRequest ();
        }
      break;
    case WAIT_ASSOC_RESP:
      /* we have sent an assoc request so we do not need to
         re-send an assoc request right now. We just need to
         wait until either assoc-request-timeout or until
         we get an assoc response.
       */
      break;
    case REFUSED:
      /* we have sent an assoc request and received a negative
         assoc resp. We wait until someone restarts an
         association with a given ssid.
       */
      break;
    }
}

void
StaWifiMac::AssocRequestTimeout (void)
{
  NS_LOG_FUNCTION (this);
  SetState (WAIT_ASSOC_RESP);
  SendAssociationRequest ();
}

void
StaWifiMac::ProbeRequestTimeout (void)
{
  NS_LOG_FUNCTION (this);
  SetState (WAIT_PROBE_RESP);
  SendProbeRequest ();
}

void
StaWifiMac::MissedBeacons (void)
{
  NS_LOG_FUNCTION (this);
  if (m_beaconWatchdogEnd > Simulator::Now ())
    {
      if (m_beaconWatchdog.IsRunning ())
        {
          m_beaconWatchdog.Cancel ();
        }
      m_beaconWatchdog = Simulator::Schedule (m_beaconWatchdogEnd - Simulator::Now (),
                                              &StaWifiMac::MissedBeacons, this);
      return;
    }
  NS_LOG_DEBUG ("beacon missed");
  SetState (BEACON_MISSED);
  TryToEnsureAssociated ();
}

void
StaWifiMac::RestartBeaconWatchdog (Time delay)
{
  NS_LOG_FUNCTION (this << delay);
  m_beaconWatchdogEnd = std::max (Simulator::Now () + delay, m_beaconWatchdogEnd);
  if (Simulator::GetDelayLeft (m_beaconWatchdog) < delay
      && m_beaconWatchdog.IsExpired ())
    {
      NS_LOG_DEBUG ("really restart watchdog.");
      m_beaconWatchdog = Simulator::Schedule (delay, &StaWifiMac::MissedBeacons, this);
    }
}

bool
StaWifiMac::IsAssociated (void) const
{
  return m_state == ASSOCIATED;
}

bool
StaWifiMac::IsWaitAssocResp (void) const
{
  return m_state == WAIT_ASSOC_RESP;
}

void
StaWifiMac::Enqueue (Ptr<const Packet> packet, Mac48Address to)
{	
  NS_LOG_FUNCTION (this << packet << to);
  if (!IsAssociated ())
    {
      NotifyTxDrop (packet);
      TryToEnsureAssociated ();
      return;
    }
  WifiMacHeader hdr;

  //If we are not a QoS AP then we definitely want to use AC_BE to
  //transmit the packet. A TID of zero will map to AC_BE (through \c
  //QosUtilsMapTidToAc()), so we use that as our default here.
  uint8_t tid = 0;

  //For now, an AP that supports QoS does not support non-QoS
  //associations, and vice versa. In future the AP model should
  //support simultaneously associated QoS and non-QoS STAs, at which
  //point there will need to be per-association QoS state maintained
  //by the association state machine, and consulted here.
  if (m_qosSupported)
    {
      hdr.SetType (WIFI_MAC_QOSDATA);
      hdr.SetQosAckPolicy (WifiMacHeader::NORMAL_ACK);
      hdr.SetQosNoEosp ();
      hdr.SetQosNoAmsdu ();
      //Transmission of multiple frames in the same TXOP is not
      //supported for now
      hdr.SetQosTxopLimit (0);

      //Fill in the QoS control field in the MAC header
      tid = QosUtilsGetTidForPacket (packet);
      //Any value greater than 7 is invalid and likely indicates that
      //the packet had no QoS tag, so we revert to zero, which'll
      //mean that AC_BE is used.
      if (tid > 7)
        {
          tid = 0;
        }
      hdr.SetQosTid (tid);
    }
  else
    {
      hdr.SetTypeData ();
    }
  if (m_htSupported || m_vhtSupported)
    {
      hdr.SetNoOrder ();
    }

  hdr.SetAddr1 (GetBssid ());
  hdr.SetAddr2 (m_low->GetAddress ());
  hdr.SetAddr3 (to);
  hdr.SetDsNotFrom ();
  hdr.SetDsTo ();

  if (m_qosSupported)
    {
      //Sanity check that the TID is valid
      NS_ASSERT (tid < 8);
      
      //Added by Najwan [-Start-]
		
		//Check if the flow is VI
		if(tid==4 || tid==5){
			//std::cout << "Video" << std::endl;
			
			//Check all the packet info by sending to CheckPacketInfo()
			//=======================================================
			Ptr<Packet> packet_copy = packet->Copy();	//NK: Copy packet to be sent to CheckPacketInfo()
			CheckPacketInfo(packet_copy);				//NK: Send packet_copy to CheckPacketInfo()
					
			//Get the packet information
			Mac48Address sender = m_low->GetAddress ();
			Mac48Address receiver = to;
			
			Ipv4Address SourceIP = GetSourceIP();
			Ipv4Address DestinationIP = GetDestinationIP();
			uint32_t SourcePort = GetSourcePort();
			uint32_t SeqId = GetSeqId();
			int FrameTypeValue = GetFrameTypeValue();
			std::string FrameTypeString = GetFrameTypeString();
			//uint8_t Tid = GetQosTid();
			uint32_t NodeId = GetNodeId();
			uint32_t DevId = GetDevId();
			uint32_t AppId = GetAppId();
			
			//Print the packet information
			std::cout << "StaWifiMac - SourceIP: " 			<< SourceIP << std::endl;
			std::cout << "StaWifiMac - DestinationIP: " 	<< DestinationIP << std::endl;
			std::cout << "StaWifiMac - SourcePort: " 		<< SourcePort << std::endl;
			std::cout << "StaWifiMac - SeqId: " 			<< SeqId << std::endl;
			std::cout << "StaWifiMac - FrameTypeValue: "	<< FrameTypeValue << std::endl;
			std::cout << "StaWifiMac - FrameTypeString: "	<< FrameTypeString << std::endl;
			//std::cout << "StaWifiMac - Tid: "				<< (int)Tid << std::endl;
			std::cout << "StaWifiMac - NodeId: " 			<< NodeId << std::endl;
			std::cout << "StaWifiMac - DevId: " 			<< DevId << std::endl;
			std::cout << "StaWifiMac - AppId: " 			<< AppId << std::endl;
			std::cout << "StaWifiMac - Sender: "	 		<< sender << std::endl;
			std::cout << "StaWifiMac - Receiver: " 			<< receiver << std::endl;
			
			//Setup pointers for EvalvidServer application
			//============================================
			Ptr<Node> n = NodeList::GetNode(NodeId);
			Ptr<EvalvidServer> EvalServ = DynamicCast<EvalvidServer> (n->GetApplication(AppId));
			std::string VidContentType = EvalServ->GetVideoContentType();
			std::cout << "StaWifiMac - Vid Content Type: " << VidContentType << std::endl;
			double TotalI = EvalServ->GetTotalHPackets();
			double TotalP = EvalServ->GetTotalPPackets();
			double TotalB = EvalServ->GetTotalBPackets();
			std::cout << "StaWifiMac - TotalI: " << TotalI << std::endl;
			std::cout << "StaWifiMac - TotalP: " << TotalP << std::endl;
			std::cout << "StaWifiMac - TotalB: " << TotalB << std::endl;
						
			//Setup pointers for queue
			//========================
			PointerValue PtrNVo, PtrNVi, PtrNBe, PtrNBk;
			
			//Get the device where this packet belong so that we can access the device's queue
			Ptr<WifiNetDevice> mobile_device = DynamicCast<WifiNetDevice> (n->GetDevice (DevId));
			//std::cout << "Mobile Device Address: " << mobile_device->GetAddress() << std::endl;
			Ptr<WifiMac> mac = mobile_device->GetMac();
			Ptr<RegularWifiMac> rmac = mac->GetObject<RegularWifiMac> ();
			
			rmac->GetAttribute("VO_EdcaTxopN", PtrNVo);
			rmac->GetAttribute("VI_EdcaTxopN", PtrNVi);
			rmac->GetAttribute("BE_EdcaTxopN", PtrNBe);
			rmac->GetAttribute("BK_EdcaTxopN", PtrNBk);
			
			//Get EDCA VO Acessor
			Ptr<EdcaTxopN> EdcaNVo;
			PtrNVo.GetAccessor(EdcaNVo);
			Ptr<WifiMacQueue> MacQueueNVo = EdcaNVo->GetEdcaQueue ();
			uint32_t NVoSize = MacQueueNVo->GetSize();
			std::cout << "NVoSize: " << (int)NVoSize << std::endl;
			
			//Get EDCA VI Acessor
			Ptr<EdcaTxopN> EdcaNVi;
			PtrNVi.GetAccessor(EdcaNVi);
			Ptr<WifiMacQueue> MacQueueNVi = EdcaNVi->GetEdcaQueue ();
			uint32_t NViSize = MacQueueNVi->GetSize();
			std::cout << "NViSize: " << (int)NViSize << std::endl;
			
			//Get EDCA BE Acessor
			Ptr<EdcaTxopN> EdcaNBe;
			PtrNBe.GetAccessor(EdcaNBe);
			Ptr<WifiMacQueue> MacQueueNBe = EdcaNBe->GetEdcaQueue ();
			uint32_t NBeSize = MacQueueNBe->GetSize();
			std::cout << "NBeSize: " << (int)NBeSize << std::endl;
			
			//Get EDCA BK Acessor
			Ptr<EdcaTxopN> EdcaNBk;
			PtrNBk.GetAccessor(EdcaNBk);
			Ptr<WifiMacQueue> MacQueueNBk = EdcaNBk->GetEdcaQueue ();
			uint32_t NBkSize = MacQueueNBk->GetSize();
			std::cout << "NBkSize: " << (int)NBkSize << std::endl;		
			
			//Get Queue Case selection from user
			std::cout << "m_qcase: " << m_qcase << "\n"<< std::endl;
			
			std::string ViContents = "";
			
			if(!MacQueueNVi->IsEmpty()){
				ViContents = MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver);  
				std::cout << "ViContents: " << ViContents << "\n"<< std::endl;
			} else {
			
				ViContents = "|N/A|";
			}
			
			std::ofstream CollectViQueueSize;
			CollectViQueueSize.open ("CollectQueueSize.out",std::ios_base::app);
			CollectViQueueSize << Simulator::Now().GetSeconds() << "\t\t" << (int)NBkSize << "\t" << (int)NBeSize << "\t"  << (int)NViSize << "\t"  << (int)NVoSize << "\n";
			CollectViQueueSize.close();
						
			switch (m_qcase) {
		
				case 1:{	//Default queueing
					QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
			
				} break;
			
				case 2:{	//If the incoming packet is I-Frame && Video queue is full, channel the incoming I-Frames Voice queue
					
					std::cout << "IFrameToVoIfViFull" << std::endl;
					IFrameToVoIfViFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort,FrameTypeValue, SeqId);
			
				} break;
				
				case 3:{	//If the incoming packet is I-Frame && Video Queue is full && Voice Queue is less than 50% full, channel the incoming I-Frame to Voice Queue
					
					std::cout << "IFrameToVoIfViFullVoLess50" << std::endl;
					IFrameToVoIfViFullVoLess50(tid, packet, hdr, MacQueueNVo, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId);
					
				} break;
				
				case 4:{	//If the Video Queue is 80% full, channel the P-Frame and B-Frame to BE to give opportunity to I-Frame to use the Video queue. //This is to preserve the vieo frame by saving the I-Frame without affecting Voice traffic flow.
					
					
					std::cout << "PAndBFrameToBeIfViQueueMore80" << std::endl;
					PAndBFrameToBeIfViQueueMore80(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId);
					
				} break;
				
				case 5:{	//Remove Any B-Frame to acommodate Own I-Frame
					
					std::cout << "RemoveAnyBFrameForOwnIFrameIfViQueueFull" << std::endl;
					
					if (FrameTypeValue==1){
						RemoveAnyBFrameForOwnIFrameIfViQueueFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId, AppId);
						
							
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
					
					
			
				} break;
				
				case 6:{	//Remove Own B-Frame to acommodate Own I-Frame
					
					if (FrameTypeValue==1){
						std::cout << "RemoveOwnBFrameForOwnIFrameIfViQueueFull" << std::endl;
						RemoveOwnBFrameForOwnIFrameIfViQueueFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
					
			
				} break;
				
				case 7:{	//ComboCase1Case6
			
					std::cout << "ComboCase1Case6" << std::endl;
					
					if (SourcePort==4012){
						RemoveAnyBFrameForOwnIFrameIfViQueueFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId, AppId);
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					} 
			
				} break;
				
				case 8:{	//RemoveAnyBFrameWhileQoEAbove30 RAPB if QoE < 30
					
					if (FrameTypeValue==1){
						std::cout << "RemoveAnyBFrameIfQoEBelow30" << std::endl;
						RemoveAnyBFrameIfQoEBelow30(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
			
				} break;
				
				case 9:{	std::cout << "CASE 9" << std::endl; //RemoveOwnBFrameWhileQoEAbove30 ROPB if QoE < 30
					
					if (FrameTypeValue==1){
						RemoveOwnBFrameIfQoEBelow30 (tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
			
				} break;
				
				case 10:{	
					
					//  Only Vi1 has the speciality of Case 5
					//  So, please check the AppId first.  If AppId=0, go to Case 5's function
					//  Else, queue as default.
			
					if (AppId==m_selectapp){
						
						std::cout << "RemoveAnyBFrameForOwnIFrameIfViQueueFull" << std::endl;
						
						if (FrameTypeValue==1){
							RemoveAnyBFrameForOwnIFrameIfViQueueFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId, AppId);
							
								
						} else {
							QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
						}
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
			
				} break;
				
				case 11:{	
			
					//  Only Vi1 has the speciality of Case 6
					//  So, please check the AppId first.  If AppId=0, go to Case 6's function
					//  Else, queue as default.
					
					if (AppId==m_selectapp){
						
						std::cout << "RemoveOwnBFrameForOwnIFrameIfViQueueFull" << std::endl;
						RemoveOwnBFrameForOwnIFrameIfViQueueFull(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
					
				
			
				} break;
				
				case 12:{	
			
					//  Only Vi1 has the speciality of Case 8
					//  So, please check the AppId first.  If AppId=0, go to Case 8's function
					//  Else, queue as default.
					
					if (AppId==m_selectapp){
						
						std::cout << "RemoveAnyBFrameIfQoEBelow28" << std::endl;
						RemoveAnyBFrameIfQoEBelow30(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
	
				} break;
				
				case 13:{	
			
					//  Only Vi1 has the speciality of Case 9
					//  So, please check the AppId first.  If AppId=0, go to Case 9's function
					//  Else, queue as default.
					
					if (AppId==m_selectapp){
						
						RemoveOwnBFrameIfQoEBelow30 (tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
	
				} break;
				
				case 14:{	//RemoveAnyBFrameIf PSNR<30 and Queue is Full
					
					if (FrameTypeValue==1){
						std::cout << "RemoveOwnBFrameHybrid" << std::endl;
						RemoveAnyBFrameHybrid(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
						
					} else {
						QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
					}
			
				} break;
				
				case 15:{	//RemoveOwnBFrameIf PSNR<30 and Queue is Full
				
				if (FrameTypeValue==1){
					std::cout << "RemoveOwnBFrameHybrid" << std::endl;
					RemoveOwnBFrameHybrid(tid, packet, hdr, MacQueueNVi, receiver, EvalServ, SourcePort, FrameTypeValue, TotalI, TotalP, TotalB, VidContentType, SeqId, AppId);
					
				} else {
					QueueDefault(tid, packet, hdr, MacQueueNVi, SourcePort, AppId, VidContentType, TotalI, TotalP, TotalB);
				}
			
				} break;
			
				
				default:
					std::cout << "Unknown qcase" << std::endl;
			}
						
			//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);  Commented, since packet are sent by a case based.
			
			
		} else {
			//std::cout << "Non-Video" << std::endl;
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}

		//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);	//Commented to avoid duplications of packet queueing
    }
  else
    {
      m_dca->Queue (packet, hdr);
    }
}

//Added by Najwan [-START-]


//CASE 15:  If Current Predicted PSNR is below 30, save the I-Frame (Remove own B-Frame to accommodate own I-Frame)
//================================================================================================================
void StaWifiMac::RemoveOwnBFrameHybrid (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE 15: If Current Predicted PSNR is below 30 and the Queue is Full, Remove B-Frame to accommodate I-Frame\n" << std::endl;
	
	//Default queue For Test mode purposes
	//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	
	//Check current PREDICTED Qoe
	//===========================
	//(1) Open Log_PacketDrop.out (..to calculate the number of dropped I,P and B)
	//(2) Count the I, P and B packets that has been dropped based on the AppId
	//(3) Calculate predicted PSNR
	//(4) If Incoming packet is I AND QoE < 30 AND Queue is Full, remove the B-Frame packets (based on the same AppId as the incoming I Frame packet) from the queue. Then enqueue the I-Frame packets.
	//(5) Else, enqueue as usual.
	
	
	//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
	
	
	//(1) Open Log_PacketDrop.out (..to calculate the number of dropped I,P and B)
	//===========================================================================

	//Create file object.
	std::ifstream FindDroppedPacket("Log_PacketDrop.out");
	uint32_t IDrop=0, PDrop=0, BDrop=0;	
	
	//Check if file exist
	if (FindDroppedPacket){

		//Create variable for every column filed in Log_PacketDrop.out
		Time f_time;
		//Ipv4Address source, destination;
		std::string f_source, f_destination;
		uint32_t f_SrcPort, f_AppId, f_SeqId;
		std::string f_FrameType, f_reason;
		uint8_t f_qosTid;
			
		std::cout << "File is opened" << std::endl;
	
	//(2) Count the I, P and B packets that has been dropped based on the AppId 
	//==========================================================================
		while (FindDroppedPacket >> f_time >> f_source >> f_destination >> f_SrcPort >> f_AppId >> f_FrameType >> f_qosTid >> f_SeqId >> f_reason){
		
			//If I-Frame packet has been dropped, increase the IDrop counter
			if (f_FrameType =="I" && f_AppId==AppId){					
				IDrop++;
				SetIDropCounter(IDrop);
					
			} else if (f_FrameType =="P" && f_AppId==AppId){
				PDrop++;
				SetPDropCounter(PDrop);
					
			} else if (f_FrameType =="B" && f_AppId==AppId){
				BDrop++;
				SetBDropCounter(BDrop);
			}
		}
			
		FindDroppedPacket.close();	
		std::cout << "File is closed" << std::endl;		
	}
	
	double HLossPercentage=0, PLossPercentage=0, BLossPercentage=0;
	float PredictedPSNR=-1, PredictedPSNRroundup=-1, PredictedOverallLoss=-1, PredictedOverallLossroundup=-1;

	HLossPercentage = (GetIDropCounter()/TotalI)*100;
	PLossPercentage = (GetPDropCounter()/TotalP)*100;
	BLossPercentage = (GetBDropCounter()/TotalB)*100;
	
	std::cout << "IDrop:\t" << GetIDropCounter() << "/" << TotalI << "\t" << HLossPercentage << "%" << std::endl;
	std::cout << "PDrop:\t" << GetPDropCounter() << "/" << TotalP << "\t" << PLossPercentage << "%" << std::endl;
	std::cout << "BDrop:\t" << GetBDropCounter() << "/" << TotalB << "\t" << BLossPercentage << "%" << std::endl;
	
	//(3) Calculate predicted PSNR
	//============================
	if(VidContentType == "st_akiyo_cif.st"){
		// Akiyo Predicted PSNR
		// PSNR = 41.92 - 0.136H - 0.052P - 0.010B : Clean version
		PredictedPSNR = 41.92 - (0.136*HLossPercentage) - (0.0520*PLossPercentage) - (0.0108*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionAkiyo.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_silent_cif.st") {
		//Silent predicted PSNR
		//PSNR = 32.87 - 0.08H - 0.053P - 0.01B
		PredictedPSNR = 32.87 - (0.08*HLossPercentage) - (0.053*PLossPercentage) - (0.01*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionSilent.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_carphone_cif.st") {
		
		//PSNR = 31.315 - 0.084I - 0.063P - 0.011B
		PredictedPSNR = 31.315 - (0.084*HLossPercentage) - (0.063*PLossPercentage) - (0.011*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionCarphone.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_foreman_cif.st") {
		
		//PSNR = 29.903 -0.084I - 0.08P - 0.018B
		PredictedPSNR = 29.903 - (0.084*HLossPercentage) - (0.08*PLossPercentage) - (0.018*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionForeman.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_football_cif.st") {
		
		//PSNR = 26.86 -0.047H - 0.069P - 0.026B
		PredictedPSNR = 26.86 - (0.047*HLossPercentage) - (0.069*PLossPercentage) - (0.026*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionFootball.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_stefan_cif.st") {

		//PSNR = 25.121 - 0.046I - 0.057P - 0.023B
		PredictedPSNR = 25.121 - (0.046*HLossPercentage) - (0.057*PLossPercentage) - (0.023*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionStefan.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_highway_cif.st") {
		//Highway predicted PSNR
		//PSNR = 30.472-0.011H-0.015P-0.09B
		//PSNR = 34.64 - 0.073H - 0.063P - 0.014B  (this is the formula for -g12)
		
		PredictedPSNR = 34.64 - (0.073*HLossPercentage) - (0.063*PLossPercentage) - (0.014*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
								
		//(4) If Incoming packet is I AND QoE < 30 AND Queue is Full, remove own B-Frame packets (based on the same AppId as the incoming I Frame packet) from the queue. Then enqueue the I-Frame packets.
		//=================================================================================================================================================================================================
		
		double ViSize = MacQueueNVi->GetSize();
		double ViMaxSize = MacQueueNVi->GetMaxSize();
		
		
		if((PredictedPSNR < m_psnrthreshold) && (ViSize==ViMaxSize)){
		
			std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
		
			//Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
			bool RemoveOwnBAttempt = MacQueueNVi->RemoveBFrameByAppId(AppId);
			
			if (RemoveOwnBAttempt){
				std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			} else {
				
				//Enqueue the incoming I-Frame, regardless
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			}
		//(5) Else, enqueue as usual
		//==========================
		} else {
			
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNRroundup << "\n" << std::endl;
		
	}
	
}

//CASE 14:  If Current Predicted PSNR is below 30 and QUEUE is Full, Remove any B-Frame
//=======================================================================================
void StaWifiMac::RemoveAnyBFrameHybrid (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE14: RemoveAnyBFrameHybrid\n" << std::endl;
	
	// Only I-Frame packets are allowed to enter here
	
	//Check current PROJECTED Qoe
	//===========================
	//(1) Have the QoE equation ready, based on the Content Type
	//(2) Open the Log_PacketDrop file
	//(3) Count the I, P and B packets that has been dropped based on the AppId
	//(4) Calculate the current projected QoE, based on the equation in (1).
	//(5) If Incoming packet is I OR P AND QoE < 35, then remove the B-Frame packets (based on the same AppId as the incoming I or P Frame packet) from the queue. Then enqueue the I and P-Frame packets.
	//(6) Else, enqueue as usual.
	
	
	//Test Mode: Calculate predicted QoE
	//==================================
	//1. Open Log_PacketDrop.out
	//2. Count dropped IPB packets with the same AppId of the incoming I and P packet
	
	
	//1. Open Log_PacketDrop.out
	//==========================================================
	//  - Motive is to calculate the numbers dropped I, P and B to predict the current PSNR
	
	//Create file object.
	std::ifstream FindDroppedPacket("Log_PacketDrop.out");
	uint32_t IDrop=0, PDrop=0, BDrop=0;	
	
	//Check if file exist
	if (FindDroppedPacket){
			
		//Create variable for every column filed in Log_PacketDrop.out
		Time f_time;
		//Ipv4Address source, destination;
		std::string f_source, f_destination;
		uint32_t f_SrcPort, f_AppId, f_SeqId;
		std::string f_FrameType, f_reason;
		uint8_t f_qosTid;
			
		std::cout << "File is opened" << std::endl;
				
		while (FindDroppedPacket >> f_time >> f_source >> f_destination >> f_SrcPort >> f_AppId >> f_FrameType >> f_qosTid >> f_SeqId >> f_reason){
		
			//If I-Frame packet has been dropped, increase the IDrop counter
			if (f_FrameType =="I" && f_AppId==AppId){					
				IDrop++;
				SetIDropCounter(IDrop);
					
			} else if (f_FrameType =="P" && f_AppId==AppId){
				PDrop++;
				SetPDropCounter(PDrop);
					
			} else if (f_FrameType =="B" && f_AppId==AppId){
				BDrop++;
				SetBDropCounter(BDrop);
			}
		}
			
		FindDroppedPacket.close();	
		std::cout << "File is closed" << std::endl;		
	}
	
	double HLossPercentage=0, PLossPercentage=0, BLossPercentage=0;
	float PredictedPSNR=-1, PredictedPSNRroundup=-1, PredictedOverallLoss=-1, PredictedOverallLossroundup=-1;

	HLossPercentage = (GetIDropCounter()/TotalI)*100;
	PLossPercentage = (GetPDropCounter()/TotalP)*100;
	BLossPercentage = (GetBDropCounter()/TotalB)*100;
	
	std::cout << "IDrop:\t" << GetIDropCounter() << "/" << TotalI << "\t" << HLossPercentage << "%" << std::endl;
	std::cout << "PDrop:\t" << GetPDropCounter() << "/" << TotalP << "\t" << PLossPercentage << "%" << std::endl;
	std::cout << "BDrop:\t" << GetBDropCounter() << "/" << TotalB << "\t" << BLossPercentage << "%" << std::endl;
	
	if(VidContentType == "st_akiyo_cif.st"){
		// Akiyo Predicted PSNR
		// PSNR = 41.92 - 0.136H - 0.052P - 0.010B : Clean version
		PredictedPSNR = 41.92 - (0.136*HLossPercentage) - (0.0520*PLossPercentage) - (0.0108*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionAkiyo.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_silent_cif.st") {
		//Silent predicted PSNR
		//PSNR = 32.87 - 0.08H - 0.053P - 0.01B
		PredictedPSNR = 32.87 - (0.08*HLossPercentage) - (0.053*PLossPercentage) - (0.01*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionSilent.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_carphone_cif.st") {
		
		//PSNR = 31.315 - 0.084I - 0.063P - 0.011B
		PredictedPSNR = 31.315 - (0.084*HLossPercentage) - (0.063*PLossPercentage) - (0.011*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionCarphone.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_foreman_cif.st") {
		
		//PSNR = 29.903 -0.084I - 0.08P - 0.018B
		PredictedPSNR = 29.903 - (0.084*HLossPercentage) - (0.08*PLossPercentage) - (0.018*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionForeman.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_football_cif.st") {
		
		//PSNR = 26.86 -0.047H - 0.069P - 0.026B
		PredictedPSNR = 26.86 - (0.047*HLossPercentage) - (0.069*PLossPercentage) - (0.026*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionFootball.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_stefan_cif.st") {

		//PSNR = 25.121 - 0.046I - 0.057P - 0.023B
		PredictedPSNR = 25.121 - (0.046*HLossPercentage) - (0.057*PLossPercentage) - (0.023*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionStefan.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_highway_cif.st") {
		//Highway predicted PSNR
		//PSNR = 30.472-0.011H-0.015P-0.09B
		//PSNR = 34.64 - 0.073H - 0.063P - 0.014B  (this is the formula for -g12)
		
		PredictedPSNR = 34.64 - (0.073*HLossPercentage) - (0.063*PLossPercentage) - (0.014*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		// Write the predicted PSNR and overall percentage loss in text file
		// Format is as follows:
		// Column 1:  Predicted PSNR
		// Column 2:  Overall loss (I + P + B Loss in percentage)
		
		double ViSize = MacQueueNVi->GetSize();
		double ViMaxSize = MacQueueNVi->GetMaxSize();
	
		//Only check the current PSNR
		if((PredictedPSNR < m_psnrthreshold) && (ViSize==ViMaxSize)){
		
			std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
			std::cout << "Cur ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
			//Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
			bool RemoveAnyBAttempt = MacQueueNVi->RemoveAnyBFrame();
			
			if (RemoveAnyBAttempt){
				std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			} else {
				
				//Enqueue the incoming I-Frame, regardless
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			}
		//(5) Else, enqueue as usual
		//==========================
		} else {
			
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
				
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNRroundup << "\n" << std::endl;
		
	}
	
}

//CASE 9:  If Current Predicted PSNR is below 30, save the I-Frame (Remove own B-Frame to accommodate own I-Frame)
//================================================================================================================
void StaWifiMac::RemoveOwnBFrameIfQoEBelow30 (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver,
										   Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE 9: If Current Predicted PSNR is below 30, save the I-Frame\n" << std::endl;
	
	//Default queue For Test mode purposes
	//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	
	//Check current PROJECTED Qoe
	//===========================
	//(1) Open Log_PacketDrop.out (..to calculate the number of dropped I,P and B)
	//(2) Count the I, P and B packets that has been dropped based on the AppId
	//(3) Calculate predicted PSNR
	//(4) If Incoming packet is I AND QoE < 25, remove the B-Frame packets (based on the same AppId as the incoming I Frame packet) from the queue. Then enqueue the I-Frame packets.
	//(5) Else, enqueue as usual.
	
	
	//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
	
	
	//(1) Open Log_PacketDrop.out (..to calculate the number of dropped I,P and B)
	//===========================================================================

	//Create file object.
	std::ifstream FindDroppedPacket("Log_PacketDrop.out");
	uint32_t IDrop=0, PDrop=0, BDrop=0;	
	
	//Check if file exist
	if (FindDroppedPacket){

		//Create variable for every column filed in Log_PacketDrop.out
		Time f_time;
		//Ipv4Address source, destination;
		std::string f_source, f_destination;
		uint32_t f_SrcPort, f_AppId, f_SeqId;
		std::string f_FrameType, f_reason;
		uint8_t f_qosTid;
			
		std::cout << "File is opened" << std::endl;
	
	//(2) Count the I, P and B packets that has been dropped based on the AppId 
	//==========================================================================
		while (FindDroppedPacket >> f_time >> f_source >> f_destination >> f_SrcPort >> f_AppId >> f_FrameType >> f_qosTid >> f_SeqId >> f_reason){
		
			//If I-Frame packet has been dropped, increase the IDrop counter
			if (f_FrameType =="I" && f_AppId==AppId){					
				IDrop++;
				SetIDropCounter(IDrop);
					
			} else if (f_FrameType =="P" && f_AppId==AppId){
				PDrop++;
				SetPDropCounter(PDrop);
					
			} else if (f_FrameType =="B" && f_AppId==AppId){
				BDrop++;
				SetBDropCounter(BDrop);
			}
		}
			
		FindDroppedPacket.close();	
		std::cout << "File is closed" << std::endl;		
	}
	
	double HLossPercentage=0, PLossPercentage=0, BLossPercentage=0;
	float PredictedPSNR=-1, PredictedPSNRroundup=-1, PredictedOverallLoss=-1, PredictedOverallLossroundup=-1;

	HLossPercentage = (GetIDropCounter()/TotalI)*100;
	PLossPercentage = (GetPDropCounter()/TotalP)*100;
	BLossPercentage = (GetBDropCounter()/TotalB)*100;
	
	std::cout << "IDrop:\t" << GetIDropCounter() << "/" << TotalI << "\t" << HLossPercentage << "%" << std::endl;
	std::cout << "PDrop:\t" << GetPDropCounter() << "/" << TotalP << "\t" << PLossPercentage << "%" << std::endl;
	std::cout << "BDrop:\t" << GetBDropCounter() << "/" << TotalB << "\t" << BLossPercentage << "%" << std::endl;
	
	//(3) Calculate predicted PSNR
	//============================
	if(VidContentType == "st_akiyo_cif.st"){
		// Akiyo Predicted PSNR
		// PSNR = 41.92 - 0.136H - 0.052P - 0.010B : Clean version
		PredictedPSNR = 41.92 - (0.136*HLossPercentage) - (0.0520*PLossPercentage) - (0.0108*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionAkiyo.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_silent_cif.st") {
		//Silent predicted PSNR
		//PSNR = 32.87 - 0.08H - 0.053P - 0.01B
		PredictedPSNR = 32.87 - (0.08*HLossPercentage) - (0.053*PLossPercentage) - (0.01*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionSilent.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_carphone_cif.st") {
		
		//PSNR = 31.315 - 0.084I - 0.063P - 0.011B
		PredictedPSNR = 31.315 - (0.084*HLossPercentage) - (0.063*PLossPercentage) - (0.011*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionCarphone.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_foreman_cif.st") {
		
		//PSNR = 29.903 -0.084I - 0.08P - 0.018B
		PredictedPSNR = 29.903 - (0.084*HLossPercentage) - (0.08*PLossPercentage) - (0.018*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionForeman.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_football_cif.st") {
		
		//PSNR = 26.86 -0.047H - 0.069P - 0.026B
		PredictedPSNR = 26.86 - (0.047*HLossPercentage) - (0.069*PLossPercentage) - (0.026*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionFootball.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_stefan_cif.st") {

		//PSNR = 25.121 - 0.046I - 0.057P - 0.023B
		PredictedPSNR = 25.121 - (0.046*HLossPercentage) - (0.057*PLossPercentage) - (0.023*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionStefan.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_highway_cif.st") {
		//Highway predicted PSNR
		//PSNR = 30.472-0.011H-0.015P-0.09B
		//PSNR = 34.64 - 0.073H - 0.063P - 0.014B  (this is the formula for -g12)
		
		//PredictedPSNR = 34.64 - (0.073*HLossPercentage) - (0.063*PLossPercentage) - (0.014*BLossPercentage);
		//PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//NEW FORMULA 2022
		//H-Loss PSNR = 34.31 + 0.04x - 1.96E - 3x2
		//P-Loss PSNR = 35.41-0.11x
		//B-Loss PSNR = 35.58 – 0.04x
		
		//PSNR Estimation considers I-Loss first, then P-Loss and finally B-Loss
		//PredictedPSNR = (34.31 + (0.04*HLossPercentage) - (1.96*pow(10,-3))) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
		
		if (HLossPercentage<60){
			
			PredictedPSNR = (35.41-0.11*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
			
		} else {
			
			PredictedPSNR = (54.77-0.36*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
		
		}
		
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		std::cout << "EstimatedPSNR:\t" << PredictedPSNRroundup << "\t" << std::endl;
			
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetBDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
	
		//(4) If Incoming packet is I AND QoE < 30, remove the B-Frame packets (based on the same AppId as the incoming I Frame packet) from the queue. Then enqueue the I-Frame packets.
		//===============================================================================================================================================================================
		if((FrameTypeValue==1) && (PredictedPSNR < m_psnrthreshold)){
		
			std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
		
			//Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
			bool RemoveOwnBAttempt = MacQueueNVi->RemoveBFrameByAppId(AppId);
					
			if (RemoveOwnBAttempt){
				std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			} else {
				
				//Enqueue the incoming I-Frame, regardless
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			}
		//(5) Else, enqueue as usual
		//==========================
		} else {
			
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNRroundup << "\n" << std::endl;
		
	}
	
}


//CASE 8:  If Current Estimated PSNR is below 30, save the I-Frame (Remove any B-Frame to accomodate own I-Frame)
//===============================================================================================================
void StaWifiMac::RemoveAnyBFrameIfQoEBelow30 (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, 
										   Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE8: RemoveAnyBFrameIfQoEBelow30\n" << std::endl;
	
	//Default queue For Test mode purposes
	//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	
	//Check current PROJECTED Qoe
	//===========================
	//(1) Have the QoE equation ready, based on the Content Type
	//(2) Open the Log_PacketDrop file
	//(3) Count the I, P and B packets that has been dropped based on the AppId
	//(4) Calculate the current projected QoE, based on the equation in (1).
	//(5) If Incoming packet is I OR P AND QoE < 35, then remove the B-Frame packets (based on the same AppId as the incoming I or P Frame packet) from the queue. Then enqueue the I and P-Frame packets.
	//(6) Else, enqueue as usual.
	
	
	//Test Mode: Calculate predicted QoE
	//==================================
	//1. Open Log_PacketDrop.out
	//2. Count dropped IPB packets with the same AppId of the incoming I and P packet
	
	
	//1. Open Log_PacketDrop.out
	//==========================================================
	//  - Motive is to calculate the numbers dropped I, P and B to predict the current PSNR
	
	//Create file object.
	std::ifstream FindDroppedPacket("Log_PacketDrop.out");
	uint32_t IDrop=0, PDrop=0, BDrop=0;	
	
	//Check if file exist
	if (FindDroppedPacket){
			
		//Create variable for every column filed in Log_PacketDrop.out
		Time f_time;
		//Ipv4Address source, destination;
		std::string f_source, f_destination;
		uint32_t f_SrcPort, f_AppId, f_SeqId;
		std::string f_FrameType, f_reason;
		uint8_t f_qosTid;
			
		std::cout << "File is opened" << std::endl;
				
		while (FindDroppedPacket >> f_time >> f_source >> f_destination >> f_SrcPort >> f_AppId >> f_FrameType >> f_qosTid >> f_SeqId >> f_reason){
		
			//If I-Frame packet has been dropped, increase the IDrop counter
			if (f_FrameType =="I" && f_AppId==AppId){					
				IDrop++;
				SetIDropCounter(IDrop);
					
			} else if (f_FrameType =="P" && f_AppId==AppId){
				PDrop++;
				SetPDropCounter(PDrop);
					
			} else if (f_FrameType =="B" && f_AppId==AppId){
				BDrop++;
				SetBDropCounter(BDrop);
			}
		}
			
		FindDroppedPacket.close();	
		std::cout << "File is closed" << std::endl;		
	}
	
	double HLossPercentage=0, PLossPercentage=0, BLossPercentage=0;
	float PredictedPSNR=-1, PredictedPSNRroundup=-1, PredictedOverallLoss=-1, PredictedOverallLossroundup=-1;

	HLossPercentage = (GetIDropCounter()/TotalI)*100;
	PLossPercentage = (GetPDropCounter()/TotalP)*100;
	BLossPercentage = (GetBDropCounter()/TotalB)*100;
	
	std::cout << "IDrop:\t" << GetIDropCounter() << "/" << TotalI << "\t" << HLossPercentage << "%" << std::endl;
	std::cout << "PDrop:\t" << GetPDropCounter() << "/" << TotalP << "\t" << PLossPercentage << "%" << std::endl;
	std::cout << "BDrop:\t" << GetBDropCounter() << "/" << TotalB << "\t" << BLossPercentage << "%" << std::endl;
	
	if(VidContentType == "st_akiyo_cif.st"){
		// Akiyo Predicted PSNR
		// PSNR = 41.92 - 0.136H - 0.052P - 0.010B : Clean version
		PredictedPSNR = 41.92 - (0.136*HLossPercentage) - (0.0520*PLossPercentage) - (0.0108*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionAkiyo.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_silent_cif.st") {
		//Silent predicted PSNR
		//PSNR = 32.87 - 0.08H - 0.053P - 0.01B
		PredictedPSNR = 32.87 - (0.08*HLossPercentage) - (0.053*PLossPercentage) - (0.01*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionSilent.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_carphone_cif.st") {
		
		//PSNR = 31.315 - 0.084I - 0.063P - 0.011B
		PredictedPSNR = 31.315 - (0.084*HLossPercentage) - (0.063*PLossPercentage) - (0.011*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionCarphone.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_foreman_cif.st") {
		
		//PSNR = 29.903 -0.084I - 0.08P - 0.018B
		PredictedPSNR = 29.903 - (0.084*HLossPercentage) - (0.08*PLossPercentage) - (0.018*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionForeman.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_football_cif.st") {
		
		//PSNR = 26.86 -0.047H - 0.069P - 0.026B
		PredictedPSNR = 26.86 - (0.047*HLossPercentage) - (0.069*PLossPercentage) - (0.026*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionFootball.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_stefan_cif.st") {

		//PSNR = 25.121 - 0.046I - 0.057P - 0.023B
		PredictedPSNR = 25.121 - (0.046*HLossPercentage) - (0.057*PLossPercentage) - (0.023*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionStefan.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_highway_cif.st") {
		//Highway predicted PSNR
		//PSNR = 30.472-0.011H-0.015P-0.09B
		//PSNR = 34.64 - 0.073H - 0.063P - 0.014B  (this is the formula for -g12)
		
		//PredictedPSNR = 34.64 - (0.073*HLossPercentage) - (0.063*PLossPercentage) - (0.014*BLossPercentage);
		
		
		
		if (HLossPercentage<60){
			
			PredictedPSNR = (35.41-0.11*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
			
		} else {
			
			PredictedPSNR = (54.77-0.36*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
		
		}
		
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		std::cout << "EstimatedPSNR:\t" << PredictedPSNRroundup << "\t" << std::endl;
		
		
		
		
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
				
		//Only check the current PSNR
		if(PredictedPSNR < m_psnrthreshold){
			std::cout << "PSNRTHRESHOLD: " << m_psnrthreshold << std::endl;
			std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
			std::cout << "Cur ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
			//Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
			bool RemoveAnyBAttempt = MacQueueNVi->RemoveAnyBFrame();
			
			if (RemoveAnyBAttempt){
				std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			} else {
				
				//Enqueue the incoming I-Frame, regardless
				m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			}
		//(5) Else, enqueue as usual
		//==========================
		} else {
			
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
				
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNRroundup << "\n" << std::endl;
		
	}
	
}

void StaWifiMac::SetIDropCounter(uint32_t IDrop){
	m_IDrop = IDrop;
}

void StaWifiMac::SetPDropCounter(uint32_t PDrop){
	m_PDrop = PDrop;
}

void StaWifiMac::SetBDropCounter(uint32_t BDrop){
	m_BDrop = BDrop;
}

uint32_t StaWifiMac::GetIDropCounter(){
	return m_IDrop;
}

uint32_t StaWifiMac::GetPDropCounter(){
	return m_PDrop;
}

uint32_t StaWifiMac::GetBDropCounter(){
	return m_BDrop;
}



//CASE 7:  Remove any B-Frame but with B-Drop Budget to accommodate own I-Frame.  BDrop Budget depends on the content of the video.
//=================================================================================================================================
//void StaWifiMac::RemoveAnyBFrameWithBDropBudgetForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue){
	
//}

//CASE X:  Remove any B-Frame to accommodate own I-Frame
//======================================================
//void StaWifiMac::RemoveAnyBFrameForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId){
	//std::cout << "CASE6: RemoveAnyBFrameForOwnIFrameIfViQueueFull\n" << std::endl;
	
	////If incoming packet is I-Frame && Queue is full, get the I-Frame's SourcePort.  
	////Search for a B-Frame from the Vi Queue
	////If a B-Frame exists, remove the B-Frame.  Else, queue as usual.
	
	////Get current VI Queue size, maximum size and the ratio
	//double ViSize = MacQueueNVi->GetSize();
	//double ViMaxSize = MacQueueNVi->GetMaxSize();
		
	//if((FrameTypeValue==1) && (ViSize == ViMaxSize)){
		
		//std::cout << "Incoming: |I:" << SeqId << ":" << SourcePort << "|" << std::endl;
		
		////Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
		//bool RemoveAnyBAttempt = MacQueueNVi->RemoveAnyBFrame(SourcePort, SeqId);
		
		//if (RemoveAnyBAttempt){
			//std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
			//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		//} else {
				
			////Enqueue the incoming I-Frame, regardless
			//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		//}
		
	//} else {
		//m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	//}
//}


//CASE 6:  Remove own B-Frame to accommodate own I-Frame if VI is full
//====================================================================
void StaWifiMac::RemoveOwnBFrameForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE5: RemoveOwnBFrameForOwnIFrameIfViQueueFull\n" << std::endl;
	
	//If incoming packet is I-Frame && Queue is full, get the I-Frame's SourcePort.  
	//Search for a B-Frame with the same SourcePort as the I-Frame.  
	//If such a B-Frame exists, remove the B-Frame.  Else, queue as usual.
	
	//Get current VI Queue size, maximum size and the ratio
	double ViSize = MacQueueNVi->GetSize();
	double ViMaxSize = MacQueueNVi->GetMaxSize();
	
	if((FrameTypeValue==1) && (ViSize == ViMaxSize)){
		
		std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
		std::cout << "Cur ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
		
		//Attempt removing a B-Frame with the same AppId as the incoming I-Frame
		bool RemoveOwnBAttempt = MacQueueNVi->RemoveBFrameByAppId(AppId);
				
		if (RemoveOwnBAttempt){
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
		} else {
				
			//Enqueue the incoming I-Frame, regardless
			std::cout << "B-Frame with the same Application Id does not exist" << std::endl;
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
		
	} else {
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}
}

//CASE 5:  Remove any B-Frame to accommodate own I-Frame if VI is full
//====================================================================
void StaWifiMac::RemoveAnyBFrameForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId, uint32_t AppId){
	std::cout << "CASE5: RemoveAnyBFrameForOwnIFrameIfViQueueFull\n" << std::endl;
		
	//Get current VI Queue size, maximum size to compare the sizes
	double ViSize = MacQueueNVi->GetSize();
	double ViMaxSize = MacQueueNVi->GetMaxSize();
	
	//If a B-Frame exists in the queue, remove the B-Frame.  
	// - Else, queue as usual.
	
	if((FrameTypeValue==1) && (ViSize == ViMaxSize)){
		
		std::cout << "Incoming: |I:" << SeqId << ":" << AppId << "|" << std::endl;
		std::cout << "Cur ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
		
		//Attempt removing a B-Frame with the same SourcePort as the incoming I-Frame
		bool RemoveAnyBAttempt = MacQueueNVi->RemoveAnyBFrame();
				
		if (RemoveAnyBAttempt){
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
			std::cout << "New ViContents: " << MacQueueNVi->GetViContents (4, WifiMacHeader::ADDR1, receiver) << std::endl;
		} else {
				
			//Enqueue the incoming I-Frame, regardless
			std::cout << "B-Frame with the same Application Id does not exist" << std::endl;
			m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
		}
		
	} else {
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}

}



//CASE 4:  Send P-Frame and B-Frame to BE if VI is 80% full.  Does not involve VO at all
//====================================================================================== 
void StaWifiMac::PAndBFrameToBeIfViQueueMore80(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId){
	std::cout << "CASE4: PAndBFrameToBeIfViQueueMore80\n" << std::endl;
	
	//Get current VI Queue size and maximum size
	double ViSize = MacQueueNVi->GetSize();
	double ViMaxSize = MacQueueNVi->GetMaxSize();
	
	double ViRatio = ViSize/ViMaxSize;
	std::cout << "ViRatio: " << ViRatio << std::endl;
	
	if ((ViRatio > 0.8) && ((FrameTypeValue == 2)||FrameTypeValue == 3)){
		
		//Queue the B-Frame or P-Frame packet into BE Queue
		
		//Remove current QosTag
		Ptr<Packet> packet_copy = packet->Copy();
		QosTag OldQosTag;
		packet_copy->RemovePacketTag(OldQosTag);
		
		//Add new QosTag
		QosTag NewQosTag;
		tid = 3;
		hdr.SetQosTid(tid);
		NewQosTag.SetTid(tid);
		packet_copy->AddPacketTag(NewQosTag);
		
		if(packet_copy->PeekPacketTag(NewQosTag)){
			std::cout << "New Qos Tag: " << (int)NewQosTag.GetTid() << std::endl;
			std::cout << "New Header Qos Tid: " << (int)hdr.GetQosTid() << std::endl;
		}
		
		//Capture/ Record the P-Frames and B-Frames that was sent to BE in a text file.
		//if (FrameTypeValue==2){
			//std::cout << Simulator::Now().GetSeconds() << "\tSend this P-Frame to BE queue" << std::endl;
			//std::ofstream PAndBFrameToBeIfViQueueMore80_PFrame;
			//PAndBFrameToBeIfViQueueMore80_PFrame.open ("PAndBFrameToBeIfViQueueMore80_PFrame.out",std::ios_base::app);
			//PAndBFrameToBeIfViQueueMore80_PFrame << Simulator::Now().GetSeconds() << "\t" << "P-Frame\t" << SourcePort << "\t" << receiver << "\t" << (int)tid << "\t" << SeqId << "\n";
			//PAndBFrameToBeIfViQueueMore80_PFrame.close();
			
		//} else if (FrameTypeValue==3) {
			//std::cout << Simulator::Now().GetSeconds() << "\tSend this B-Frame to BE queue" << std::endl;
			//std::ofstream PAndBFrameToBeIfViQueueMore80_BFrame;
			//PAndBFrameToBeIfViQueueMore80_BFrame.open ("PAndBFrameToBeIfViQueueMore80_BFrame.out",std::ios_base::app);
			//PAndBFrameToBeIfViQueueMore80_BFrame << Simulator::Now().GetSeconds() << "\t" << "B-Frame\t" << SourcePort << "\t" << receiver << "\t" << (int)tid << "\t" << SeqId << "\n";
			//PAndBFrameToBeIfViQueueMore80_BFrame.close();
		//}
						
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet_copy, hdr);
		
	} else {
		std::cout << "FrameTypeValue: " << FrameTypeValue << std::endl;
		//Queue as usual
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}
	
	
}


//CASE 3: I-Frame to VO if VI is Full && VO is less than 50% full
//===============================================================
void StaWifiMac::IFrameToVoIfViFullVoLess50(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVo, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId){
	std::cout << "CASE3: IFrameToVoIfViFullVoLess50\n" << std::endl;

	//Get current VI Queue size and maximum size
	double ViSize = MacQueueNVi->GetSize();
	double ViMaxSize = MacQueueNVi->GetMaxSize();
	
	//Get current V0 queue size maximum size
	double VoSize = MacQueueNVo->GetSize();
	double VoMaxSize = MacQueueNVo->GetMaxSize();
	
	//Get the VO ratio
	double VoRatio = VoSize/VoMaxSize;
	
	//if current queue size is more than 80% of max queue size && video packet is I-Frame
	if ((ViSize==ViMaxSize) && (FrameTypeValue == 1) && (VoRatio < 0.5)){
		
		//Queue the I-Frame packet into VO Queue
		std::cout << Simulator::Now().GetSeconds() << "\tSend this I-Frame to VO queue" << std::endl;
		
		//Remove current QosTag
		Ptr<Packet> packet_copy = packet->Copy();
		QosTag OldQosTag;
		packet_copy->RemovePacketTag(OldQosTag);
		
		//Add new QosTag
		QosTag NewQosTag;
		tid = 6;
		hdr.SetQosTid(tid);
		NewQosTag.SetTid(tid);
		packet_copy->AddPacketTag(NewQosTag);
				
		if(packet_copy->PeekPacketTag(NewQosTag)){
			std::cout << "New Qos Tag: " << (int)NewQosTag.GetTid() << std::endl;
			std::cout << "New Header Qos Tid: " << (int)hdr.GetQosTid() << std::endl;
		}
		
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet_copy, hdr);
		
	} else {
		//Queue as usual
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}
}


//CASE 2: I-Frame to VO if VI is Full
//===================================
void StaWifiMac::IFrameToVoIfViFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId){
	std::cout << "CASE2: IFrameToVoIfViFull\n" << std::endl;
	//Get WifiMacQueue maximum queue size
	uint32_t ViMaxSize = MacQueueNVi->GetMaxSize();
	
	//Get current WifiMacQueue size
	uint32_t ViSize = MacQueueNVi->GetSize();
	
	//If current Vi queue size is full && video packet is I-Frame
	if ((ViSize == ViMaxSize) && FrameTypeValue == 1){
		
		//Queue the I-Frame packet into VO Queue
		std::cout << "Incoming: |I:" << SeqId << ":" << SourcePort << "|" << std::endl;
		std::cout << Simulator::Now().GetSeconds() << "\tSend this I-Frame to VO queue" << std::endl;
		
		//Remove current QosTag
		Ptr<Packet> packet_copy = packet->Copy();
		QosTag OldQosTag;
		packet_copy->RemovePacketTag(OldQosTag);
		
		//Add new QosTag
		QosTag NewQosTag;
		tid = 6;
		hdr.SetQosTid(tid);
		NewQosTag.SetTid(tid);
		packet_copy->AddPacketTag(NewQosTag);
		
		if(packet_copy->PeekPacketTag(NewQosTag)){
			std::cout << "New Qos Tag: " << (int)NewQosTag.GetTid() << std::endl;
			std::cout << "New Header Qos Tid: " << (int)hdr.GetQosTid() << std::endl;
		}
				
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet_copy, hdr);
		
	} else {
		//Queue as usual
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}
}

//CASE 1:  Default
//================
void StaWifiMac::QueueDefault(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, uint32_t SourcePort, uint32_t AppId, std::string VidContentType, double TotalI, double TotalP, double TotalB){
	std::cout << "CASE1: QueueDefault\n" << std::endl;
		
	// Predict PSNR (Start)
	// ====================
	
	std::ifstream FindDroppedPacket("Log_PacketDrop.out");
	uint32_t IDrop=0, PDrop=0, BDrop=0;	
	
	//Check if file exist
	if (FindDroppedPacket){
			
		//Create variable for every column filed in Log_PacketDrop.out
		Time f_time;
		//Ipv4Address source, destination;
		std::string f_source, f_destination;
		uint32_t f_SrcPort, f_AppId, f_SeqId;
		std::string f_FrameType, f_reason;
		uint8_t f_qosTid;
			
		std::cout << "File is opened" << std::endl;
				
		while (FindDroppedPacket >> f_time >> f_source >> f_destination >> f_SrcPort >> f_AppId >> f_FrameType >> f_qosTid >> f_SeqId >> f_reason){
		
			//If I-Frame packet has been dropped, increase the IDrop counter
			if (f_FrameType =="I" && f_AppId==AppId){					
				IDrop++;
				SetIDropCounter(IDrop);
					
			} else if (f_FrameType =="P" && f_AppId==AppId){
				PDrop++;
				SetPDropCounter(PDrop);
					
			} else if (f_FrameType =="B" && f_AppId==AppId){
				BDrop++;
				SetBDropCounter(BDrop);
			}
		}
			
		FindDroppedPacket.close();	
		std::cout << "File is closed" << std::endl;		
	}
	
	double HLossPercentage=0, PLossPercentage=0, BLossPercentage=0;
	float PredictedPSNR=-1, PredictedPSNRroundup=-1, PredictedOverallLoss=-1, PredictedOverallLossroundup=-1;

	HLossPercentage = (GetIDropCounter()/TotalI)*100;
	PLossPercentage = (GetPDropCounter()/TotalP)*100;
	BLossPercentage = (GetBDropCounter()/TotalB)*100;
	
	std::cout << "IDrop:\t" << GetIDropCounter() << "/" << TotalI << "\t" << HLossPercentage << "%" << std::endl;
	std::cout << "PDrop:\t" << GetPDropCounter() << "/" << TotalP << "\t" << PLossPercentage << "%" << std::endl;
	std::cout << "BDrop:\t" << GetBDropCounter() << "/" << TotalB << "\t" << BLossPercentage << "%" << std::endl;
	
	if(VidContentType == "st_akiyo_cif.st"){
		// Akiyo Predicted PSNR
		// PSNR = 41.92 - 0.136H - 0.052P - 0.010B : Clean version
		PredictedPSNR = 41.92 - (0.136*HLossPercentage) - (0.0520*PLossPercentage) - (0.0108*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionAkiyo.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_silent_cif.st") {
		//Silent predicted PSNR
		//PSNR = 32.87 - 0.08H - 0.053P - 0.01B
		PredictedPSNR = 32.87 - (0.08*HLossPercentage) - (0.053*PLossPercentage) - (0.01*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionSilent.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();
		
		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_carphone_cif.st") {
		
		//PSNR = 31.315 - 0.084I - 0.063P - 0.011B
		PredictedPSNR = 31.315 - (0.084*HLossPercentage) - (0.063*PLossPercentage) - (0.011*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionCarphone.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_foreman_cif.st") {
		
		//PSNR = 29.903 -0.084I - 0.08P - 0.018B
		PredictedPSNR = 29.903 - (0.084*HLossPercentage) - (0.08*PLossPercentage) - (0.018*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionForeman.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_football_cif.st") {
		
		//PSNR = 26.86 -0.047H - 0.069P - 0.026B
		PredictedPSNR = 26.86 - (0.047*HLossPercentage) - (0.069*PLossPercentage) - (0.026*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionFootball.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_stefan_cif.st") {

		//PSNR = 25.121 - 0.046I - 0.057P - 0.023B
		PredictedPSNR = 25.121 - (0.046*HLossPercentage) - (0.057*PLossPercentage) - (0.023*BLossPercentage);
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetIDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		//Write the predicted PSNR and overall percentage loss in text file
		std::ofstream PSNRPrediction;
		PSNRPrediction.open ("PSNRPredictionStefan.out");
		PSNRPrediction << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup;
		PSNRPrediction.close();

		std::cout << "\nPredicted PSNR " << VidContentType << ": " << PredictedPSNR << "\n" << std::endl;
		
	} else if (VidContentType == "st_highway_cif.st") {
		//Highway predicted PSNR
		//PSNR = 30.472-0.011H-0.015P-0.09B
		//PSNR = 34.64 - 0.073H - 0.063P - 0.014B  (this is the formula for -g12)
		
		//PredictedPSNR = 34.64 - (0.073*HLossPercentage) - (0.063*PLossPercentage) - (0.014*BLossPercentage);
		//PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//NEW FORMULA 2022 -----|
		
		//H-Loss PSNR = 34.31 + 0.04x - 1.96E - 3x2
		//P-Loss PSNR = 35.41-0.11x
		//B-Loss PSNR = 35.58 – 0.04x
		
		//PSNR Estimation considers I-Loss first, then P-Loss and finally B-Loss
		//PredictedPSNR = (34.31 + (0.04*HLossPercentage) - (1.96*pow(10,-3))) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
		
		if (HLossPercentage<60){
			
			PredictedPSNR = (35.41-0.11*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
			
		} else {
			
			PredictedPSNR = (54.77-0.36*(HLossPercentage)) - (0.11*(PLossPercentage)) - (0.04*(BLossPercentage));
		
		}
		
		
		
		
		PredictedPSNRroundup = ceilf(PredictedPSNR * 100) / 100;
		
		//----------------------|
		
		//Calculate the overall percentage of packet loss
		PredictedOverallLoss = float(((GetIDropCounter()+GetPDropCounter()+GetBDropCounter()) / (TotalI+TotalP+TotalB))*100);
		PredictedOverallLossroundup = ceilf(PredictedOverallLoss * 100) / 100;
		
		// Write the predicted PSNR and overall percentage loss in text file
		// Format is as follows:
		// Column 1:  Predicted PSNR
		// Column 2:  Overall loss (I + P + B Loss in percentage)
		
		if (AppId==0){
			
			std::ofstream PSNRPredictionHighway0;
			PSNRPredictionHighway0.open ("PSNRPredictionHighway0.out");
			PSNRPredictionHighway0 << AppId << "\t" << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup << std::endl;
			PSNRPredictionHighway0.close();
			
		} else if (AppId==1) {
			
			std::ofstream PSNRPredictionHighway1;
			PSNRPredictionHighway1.open ("PSNRPredictionHighway1.out");
			PSNRPredictionHighway1 << AppId << "\t" << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup << std::endl;
			PSNRPredictionHighway1.close();
			
		} else if (AppId==2) {
			
			std::ofstream PSNRPredictionHighway2;
			PSNRPredictionHighway2.open ("PSNRPredictionHighway2.out");
			PSNRPredictionHighway2 << AppId << "\t" << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup << std::endl;
			PSNRPredictionHighway2.close();
			
		} else if (AppId==3) {
			
			std::ofstream PSNRPredictionHighway3;
			PSNRPredictionHighway3.open ("PSNRPredictionHighway3.out");
			PSNRPredictionHighway3 << AppId << "\t" << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup << std::endl;
			PSNRPredictionHighway3.close();
			
		} else if (AppId==4) {
			
			std::ofstream PSNRPredictionHighway4;
			PSNRPredictionHighway4.open ("PSNRPredictionHighway4.out");
			PSNRPredictionHighway4 << AppId << "\t" << setprecision (2) << fixed << PredictedPSNRroundup << "\t" << PredictedOverallLossroundup << std::endl;
			PSNRPredictionHighway4.close();
			
		}
		
		//  PSNR Prediction (End)
		//  =====================
		
		m_edca[QosUtilsMapTidToAc (tid)]->Queue (packet, hdr);
	}
		
}

bool 
StaWifiMac::FindDroppedPacketExist(){
	
	std::cout << "FindDroppedPacketExist()" << std::endl;
	
	std::ifstream FindDroppedPacketExist;
	FindDroppedPacketExist.open ("Log_PacketDrop.out");
	
	if (FindDroppedPacketExist.fail()){
		
		std::cout << "Error Opening File: Log_PacketDrop.out" << std::endl;
		return false;
		
	} else {
		FindDroppedPacketExist.close();
		std::cout << "Log_PacketDrop.out exists" << std::endl;
		return true;
		
	}
}

void
StaWifiMac::CheckPacketInfo (Ptr<Packet> packet_copy){
	/* Check packet info based on metadata
	 * 1. SourceIP		(IPv4 Header)
	 * 2. DestinationIP	(IPv4 Header)
	 * 3. Source Port	(UDP Header)
	 * 4. SeqId			(SeqTs Header)
	 */
	
	Ipv4Address SourceIP, DestinationIP;
	uint32_t SourcePort = -1, SeqId = -1;
	
	packet_copy->EnablePrinting();
	
	PacketMetadata::ItemIterator metadataIterator = packet_copy->BeginItem();
	PacketMetadata::Item item;
	
	while (metadataIterator.HasNext()){
		
		item = metadataIterator.Next();
		
		//CheckIpv4Header
		//===============
		if(item.tid.GetName()=="ns3::Ipv4Header"){
			//std::cout << "ns3::Ipv4Header" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			Ipv4Header* ipv4Header = dynamic_cast<Ipv4Header*> (instance);
			NS_ASSERT(ipv4Header != 0);
			
			ipv4Header->Deserialize(item.current);
			
			// The tcp sequence can now obtain the source of the packet
			SourceIP		= ipv4Header->GetSource();
			DestinationIP	= ipv4Header->GetDestination();
			
			//Set Source / Destination Address***
			SetSourceIP(SourceIP);
			SetDestinationIP(DestinationIP);
			
			delete ipv4Header;
			//break;
		}
		
		//CheckUdpHeader
		//==============
		if(item.tid.GetName()=="ns3::UdpHeader"){
			//std::cout << "ns3::UdpHeader" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			UdpHeader* udpHeader = dynamic_cast<UdpHeader*> (instance);
			NS_ASSERT(udpHeader != 0);
			
			udpHeader->Deserialize(item.current);
			
			// The source port can now obtain the source of the packet
			SourcePort = udpHeader->GetSourcePort();
			
			//Set Source Port Number***
			SetSourcePort(SourcePort);
						
			delete udpHeader;
			//break;
		}
		
		//Check SeqTsHeader
		//=================
		if(item.tid.GetName()=="ns3::SeqTsHeader"){
			//std::cout << "ns3::SeqTsHeader" << std::endl;
			Callback<ObjectBase *> constr = item.tid.GetConstructor();
			NS_ASSERT(!constr.IsNull());
			
			// Ptr<> and DynamicCast<> won't work here as all headers are from ObjectBase, not Object
			ObjectBase *instance = constr();
			NS_ASSERT(instance != 0);
			
			SeqTsHeader* seqTsHeader = dynamic_cast<SeqTsHeader*> (instance);
			NS_ASSERT(seqTsHeader != 0);
			
			seqTsHeader->Deserialize(item.current);
			
			// The tcp sequence can now obtain the source of the packet
			//std::cout << "SeqTsHeader: " << seqTsHeader->GetSeq() << std::endl;
			SeqId = seqTsHeader->GetSeq();
			
			//Set SeqId***
			SetSeqId(SeqId);
			
			delete seqTsHeader;
			break;
		}

	}	
	
	/* Check packet info based on frame tag
	 * 1. Frame Tag Value	(Frame Type Tag)
	 * 2. Frame Type		(Frame Type Tag)
	 * 3. Qos Tid			(QoS Tag)
	 */
	
	//Check Frame Type Tag
	//====================
	FrameTypeTag frameTag;
	uint32_t FrameTypeValue;
	std::string FrameTypeString = "";
	
	if (packet_copy->PeekPacketTag(frameTag)){
		
		FrameTypeValue = frameTag.GetFrameType();
		SetFrameTypeValue(FrameTypeValue);
		
		if (FrameTypeValue == 1){
			FrameTypeString = "I";
			SetFrameTypeString(FrameTypeString);
			
		} else if (FrameTypeValue == 2){
			FrameTypeString = "P";
			SetFrameTypeString(FrameTypeString);
			
		} else if (FrameTypeValue == 3){
			FrameTypeString = "B";
			SetFrameTypeString(FrameTypeString);

		} else {
			//std::cout << "UNKNOWN FRAMETAG NUMBER" << std::endl;
			SetFrameTypeString("UNKNOWN FRAMETAG NUMBER" );
		}	
	}
	
	//Check QoS Tag
	//=============
	QosTag qosTag;
	uint8_t qosTid;
	
	if (packet_copy->PeekPacketTag(qosTag)){
		qosTid = qosTag.GetTid();
		SetQosTid(qosTid);
	
	}
	
	//Check NodeId Tag
	//=================
	NodeIdTag nodeIdTag;
	uint32_t NodeId=-1;
	
	if (packet_copy->PeekPacketTag(nodeIdTag)){
		NodeId = nodeIdTag.GetNodeId();
		SetNodeId(NodeId);
	}
	
	//Check DevId Tag
	//===============
	DevIdTag devIdTag;
	uint32_t DevId=-1;
	
	if (packet_copy->PeekPacketTag(devIdTag)){
		DevId = devIdTag.GetDevId();
		SetDevId(DevId);
	}
	
	//Check AppId Tag
	//===============
	AppIdTag appIdTag;
	uint32_t AppId=-1;
	
	if (packet_copy->PeekPacketTag(appIdTag)){
		AppId = appIdTag.GetAppId();
		SetAppId(AppId);
	}
	
}


//Setter Getter Start
//===================

//SourceIP Setter/ Getter
void StaWifiMac::SetSourceIP(Ipv4Address SourceIP){
	m_SourceIP = SourceIP;
}

Ipv4Address StaWifiMac::GetSourceIP(){
	return m_SourceIP;
}

//DestinationIP Setter/ Getter
void StaWifiMac::SetDestinationIP(Ipv4Address DestinationIP){
	m_DestinationIP = DestinationIP;
}

Ipv4Address StaWifiMac::GetDestinationIP(){
	return m_DestinationIP;
}

//SourcePort Setter/ Getter
void StaWifiMac::SetSourcePort(uint32_t SourcePort){
	m_SourcePort = SourcePort;
}

uint32_t StaWifiMac::GetSourcePort(){
	return m_SourcePort;
}

//SeqId Setter/ Getter
void StaWifiMac::SetSeqId(uint32_t SeqId){
	m_SeqId = SeqId;
}

uint32_t StaWifiMac::GetSeqId(){
	return m_SeqId;
}

//FrameTypeValue Setter/ Getter
void StaWifiMac::SetFrameTypeValue(uint32_t FrameTypeValue){
	m_FrameTypeValue = FrameTypeValue;
}

uint32_t StaWifiMac::GetFrameTypeValue(){
	return m_FrameTypeValue;
}

//FrameType Setter/ Getter
void StaWifiMac::SetFrameTypeString(std::string FrameTypeString){
	m_FrameTypeString = FrameTypeString;
}

std::string StaWifiMac::GetFrameTypeString(){
	return m_FrameTypeString;
}

//QoSTid Setter/ Getter
void StaWifiMac::SetQosTid(uint8_t QosTid){
	m_QosTid = QosTid;
}

uint8_t StaWifiMac::GetQosTid(){
	return m_QosTid;
}

//NodeId Setter/ Getter
void StaWifiMac::SetNodeId(uint32_t NodeId){
	m_NodeId = NodeId;
}

uint32_t StaWifiMac::GetNodeId(){
	return m_NodeId;
}

//DeviceId Setter/ Getter
void StaWifiMac::SetDevId(uint32_t DevId){
	m_DevId = DevId;
}

uint32_t StaWifiMac::GetDevId(){
	return m_DevId;
}

//AppId Setter/ Getter
void StaWifiMac::SetAppId(uint32_t AppId){
	m_AppId = AppId;
}

uint32_t StaWifiMac::GetAppId(){
	return m_AppId;
}


//Setter Getter End



//Added by Najwan [-END-]

void
StaWifiMac::Receive (Ptr<Packet> packet, const WifiMacHeader *hdr)
{
  NS_LOG_FUNCTION (this << packet << hdr);
  NS_ASSERT (!hdr->IsCtl ());
  if (hdr->GetAddr3 () == GetAddress ())
    {
      NS_LOG_LOGIC ("packet sent by us.");
      return;
    }
  else if (hdr->GetAddr1 () != GetAddress ()
           && !hdr->GetAddr1 ().IsGroup ())
    {
      NS_LOG_LOGIC ("packet is not for us");
      NotifyRxDrop (packet);
      return;
    }
  else if (hdr->IsData ())
    {
      if (!IsAssociated ())
        {
          NS_LOG_LOGIC ("Received data frame while not associated: ignore");
          NotifyRxDrop (packet);
          return;
        }
      if (!(hdr->IsFromDs () && !hdr->IsToDs ()))
        {
          NS_LOG_LOGIC ("Received data frame not from the DS: ignore");
          NotifyRxDrop (packet);
          return;
        }
      if (hdr->GetAddr2 () != GetBssid ())
        {
          NS_LOG_LOGIC ("Received data frame not from the BSS we are associated with: ignore");
          NotifyRxDrop (packet);
          return;
        }
      if (hdr->IsQosData ())
        {
          if (hdr->IsQosAmsdu ())
            {
              NS_ASSERT (hdr->GetAddr3 () == GetBssid ());
              DeaggregateAmsduAndForward (packet, hdr);
              packet = 0;
            }
          else
            {
              ForwardUp (packet, hdr->GetAddr3 (), hdr->GetAddr1 ());
            }
        }
      else
        {
          ForwardUp (packet, hdr->GetAddr3 (), hdr->GetAddr1 ());
        }
      return;
    }
  else if (hdr->IsProbeReq ()
           || hdr->IsAssocReq ())
    {
      //This is a frame aimed at an AP, so we can safely ignore it.
      NotifyRxDrop (packet);
      return;
    }
  else if (hdr->IsBeacon ())
    {
      MgtBeaconHeader beacon;
      packet->RemoveHeader (beacon);
      CapabilityInformation capabilities = beacon.GetCapabilities ();
      bool goodBeacon = false;
      if (GetSsid ().IsBroadcast ()
          || beacon.GetSsid ().IsEqual (GetSsid ()))
        {
          goodBeacon = true;
        }
      SupportedRates rates = beacon.GetSupportedRates ();
      for (uint32_t i = 0; i < m_phy->GetNBssMembershipSelectors (); i++)
        {
          uint32_t selector = m_phy->GetBssMembershipSelector (i);
          if (!rates.IsSupportedRate (selector))
            {
              goodBeacon = false;
            }
        }
      if ((IsWaitAssocResp () || IsAssociated ()) && hdr->GetAddr3 () != GetBssid ())
        {
          goodBeacon = false;
        }
      if (goodBeacon)
        {
          Time delay = MicroSeconds (beacon.GetBeaconIntervalUs () * m_maxMissedBeacons);
          RestartBeaconWatchdog (delay);
          SetBssid (hdr->GetAddr3 ());
          bool isShortPreambleEnabled = capabilities.IsShortPreamble ();
          if (m_erpSupported)
            {
              ErpInformation erpInformation = beacon.GetErpInformation ();
              isShortPreambleEnabled &= !erpInformation.GetBarkerPreambleMode ();
              if (erpInformation.GetUseProtection() == true)
                {
                  m_stationManager->SetUseNonErpProtection (true);
                }
              else
                {
                  m_stationManager->SetUseNonErpProtection (false);
                }
              if (capabilities.IsShortSlotTime () == true)
                {
                  //enable short slot time
                  SetSlot (MicroSeconds (9));
                }
              else
                {
                  //disable short slot time
                  SetSlot (MicroSeconds (20));
                }
            }
          m_stationManager->SetShortPreambleEnabled (isShortPreambleEnabled);
          m_stationManager->SetShortSlotTimeEnabled (capabilities.IsShortSlotTime ());
        }
      if (goodBeacon && m_state == BEACON_MISSED)
        {
          SetState (WAIT_ASSOC_RESP);
          SendAssociationRequest ();
        }
      return;
    }
  else if (hdr->IsProbeResp ())
    {
      if (m_state == WAIT_PROBE_RESP)
        {
          MgtProbeResponseHeader probeResp;
          packet->RemoveHeader (probeResp);
          CapabilityInformation capabilities = probeResp.GetCapabilities ();
          if (!probeResp.GetSsid ().IsEqual (GetSsid ()))
            {
              //not a probe resp for our ssid.
              return;
            }
          SupportedRates rates = probeResp.GetSupportedRates ();
          for (uint32_t i = 0; i < m_phy->GetNBssMembershipSelectors (); i++)
            {
              uint32_t selector = m_phy->GetBssMembershipSelector (i);
              if (!rates.IsSupportedRate (selector))
                {
                  return;
                }
            }
          for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
            {
              WifiMode mode = m_phy->GetMode (i);
              uint8_t nss = 1; // Assume 1 spatial stream
              if (rates.IsSupportedRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, nss)))
                {
                  m_stationManager->AddSupportedMode (hdr->GetAddr2 (), mode);
                  if (rates.IsBasicRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, nss)))
                    {
                      m_stationManager->AddBasicMode (mode);
                    }
                }
            }
            
          bool isShortPreambleEnabled = capabilities.IsShortPreamble ();
          if (m_erpSupported)
            {
              bool isErpAllowed = false;
              for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
              {
                WifiMode mode = m_phy->GetMode (i);
                if (mode.GetModulationClass () == WIFI_MOD_CLASS_ERP_OFDM && rates.IsSupportedRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, 1)))
                  {
                    isErpAllowed = true;
                    break;
                  }
              }
              if (!isErpAllowed)
                {
                  //disable short slot time and set cwMin to 31
                  SetSlot (MicroSeconds (20));
                  ConfigureContentionWindow (31, 1023);
                }
              else
                {
                  ErpInformation erpInformation = probeResp.GetErpInformation ();
                  isShortPreambleEnabled &= !erpInformation.GetBarkerPreambleMode ();
                  if (m_stationManager->GetShortSlotTimeEnabled ())
                    {
                      //enable short slot time
                      SetSlot (MicroSeconds (9));
                    }
                  else
                    {
                      //disable short slot time
                      SetSlot (MicroSeconds (20));
                    }
                }
            }
          m_stationManager->SetShortPreambleEnabled (isShortPreambleEnabled);
          m_stationManager->SetShortSlotTimeEnabled (capabilities.IsShortSlotTime ());
          SetBssid (hdr->GetAddr3 ());
          Time delay = MicroSeconds (probeResp.GetBeaconIntervalUs () * m_maxMissedBeacons);
          RestartBeaconWatchdog (delay);
          if (m_probeRequestEvent.IsRunning ())
            {
              m_probeRequestEvent.Cancel ();
            }
          SetState (WAIT_ASSOC_RESP);
          SendAssociationRequest ();
        }
      return;
    }
  else if (hdr->IsAssocResp ())
    {
      if (m_state == WAIT_ASSOC_RESP)
        {
          MgtAssocResponseHeader assocResp;
          packet->RemoveHeader (assocResp);
          if (m_assocRequestEvent.IsRunning ())
            {
              m_assocRequestEvent.Cancel ();
            }
          if (assocResp.GetStatusCode ().IsSuccess ())
            {
              SetState (ASSOCIATED);
              NS_LOG_DEBUG ("assoc completed");
              CapabilityInformation capabilities = assocResp.GetCapabilities ();
              SupportedRates rates = assocResp.GetSupportedRates ();
              bool isShortPreambleEnabled = capabilities.IsShortPreamble ();
              if (m_erpSupported)
                {
                  bool isErpAllowed = false;
                  for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
                  {
                    WifiMode mode = m_phy->GetMode (i);
                    if (mode.GetModulationClass () == WIFI_MOD_CLASS_ERP_OFDM && rates.IsSupportedRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, 1)))
                      {
                        isErpAllowed = true;
                        break;
                      }
                  }
                  if (!isErpAllowed)
                    {
                      //disable short slot time and set cwMin to 31
                      SetSlot (MicroSeconds (20));
                      ConfigureContentionWindow (31, 1023);
                    }
                  else
                    {
                      ErpInformation erpInformation = assocResp.GetErpInformation ();
                      isShortPreambleEnabled &= !erpInformation.GetBarkerPreambleMode ();
                      if (m_stationManager->GetShortSlotTimeEnabled ())
                        {
                          //enable short slot time
                          SetSlot (MicroSeconds (9));
                        }
                      else
                        {
                          //disable short slot time
                          SetSlot (MicroSeconds (20));
                        }
                    }
                }
              m_stationManager->SetShortPreambleEnabled (isShortPreambleEnabled);
              m_stationManager->SetShortSlotTimeEnabled (capabilities.IsShortSlotTime ());
              if (m_htSupported)
                {
                  HtCapabilities htcapabilities = assocResp.GetHtCapabilities ();
                  HtOperations htOperations = assocResp.GetHtOperations ();
                  m_stationManager->AddStationHtCapabilities (hdr->GetAddr2 (), htcapabilities);
                }
              if (m_vhtSupported)
                {
                  VhtCapabilities vhtcapabilities = assocResp.GetVhtCapabilities ();
                  m_stationManager->AddStationVhtCapabilities (hdr->GetAddr2 (), vhtcapabilities);
                }

              for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
                {
                  WifiMode mode = m_phy->GetMode (i);
                  uint8_t nss = 1; // Assume 1 spatial stream
                  if (rates.IsSupportedRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, nss)))
                    {
                      m_stationManager->AddSupportedMode (hdr->GetAddr2 (), mode);
                      if (rates.IsBasicRate (mode.GetDataRate (m_phy->GetChannelWidth (), false, nss)))
                        {
                          m_stationManager->AddBasicMode (mode);
                        }
                    }
                }
              if (m_htSupported)
                {
                  HtCapabilities htcapabilities = assocResp.GetHtCapabilities ();
                  for (uint32_t i = 0; i < m_phy->GetNMcs (); i++)
                    {
                      WifiMode mcs = m_phy->GetMcs (i);
                      if (mcs.GetModulationClass () == WIFI_MOD_CLASS_HT && htcapabilities.IsSupportedMcs (mcs.GetMcsValue ()))
                        {
                          m_stationManager->AddSupportedMcs (hdr->GetAddr2 (), mcs);
                          //here should add a control to add basic MCS when it is implemented
                        }
                    }
                }
              if (m_vhtSupported)
                {
                  VhtCapabilities vhtcapabilities = assocResp.GetVhtCapabilities ();
                  for (uint32_t i = 0; i < m_phy->GetNMcs (); i++)
                    {
                      WifiMode mcs = m_phy->GetMcs (i);
                      if (mcs.GetModulationClass () == WIFI_MOD_CLASS_VHT && vhtcapabilities.IsSupportedTxMcs (mcs.GetMcsValue ()))
                        {
                          m_stationManager->AddSupportedMcs (hdr->GetAddr2 (), mcs);
                          //here should add a control to add basic MCS when it is implemented
                        }
                    }
                }
              if (!m_linkUp.IsNull ())
                {
                  m_linkUp ();
                }
            }
          else
            {
              NS_LOG_DEBUG ("assoc refused");
              SetState (REFUSED);
            }
        }
      return;
    }

  //Invoke the receive handler of our parent class to deal with any
  //other frames. Specifically, this will handle Block Ack-related
  //Management Action frames.
  RegularWifiMac::Receive (packet, hdr);
}

SupportedRates
StaWifiMac::GetSupportedRates (void) const
{
  SupportedRates rates;
  uint8_t nss = 1;  // Number of spatial streams is 1 for non-MIMO modes
  if (m_htSupported || m_vhtSupported)
    {
      for (uint32_t i = 0; i < m_phy->GetNBssMembershipSelectors (); i++)
        {
          rates.SetBasicRate (m_phy->GetBssMembershipSelector (i));
        }
    }
  for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
    {
      WifiMode mode = m_phy->GetMode (i);
      uint64_t modeDataRate = mode.GetDataRate (m_phy->GetChannelWidth (), false, nss);
      NS_LOG_DEBUG ("Adding supported rate of " << modeDataRate);
      rates.AddSupportedRate (modeDataRate);
    }
  return rates;
}

CapabilityInformation
StaWifiMac::GetCapabilities (void) const
{
  CapabilityInformation capabilities;
  capabilities.SetShortPreamble (m_phy->GetShortPlcpPreambleSupported () || m_erpSupported);
  capabilities.SetShortSlotTime (GetShortSlotTimeSupported () && m_erpSupported);
  return capabilities;
}

void
StaWifiMac::SetState (MacState value)
{
  if (value == ASSOCIATED
      && m_state != ASSOCIATED)
    {
      m_assocLogger (GetBssid ());
    }
  else if (value != ASSOCIATED
           && m_state == ASSOCIATED)
    {
      m_deAssocLogger (GetBssid ());
    }
  m_state = value;
}

} //namespace ns3

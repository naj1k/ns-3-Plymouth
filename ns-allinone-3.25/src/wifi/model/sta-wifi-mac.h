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
#ifndef STA_WIFI_MAC_H
#define STA_WIFI_MAC_H

#include "regular-wifi-mac.h"
#include "ns3/event-id.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"
#include "supported-rates.h"
#include "amsdu-subframe-header.h"
#include "capability-information.h"

//Added by Najwan
#include "ns3/evalvid-server.h"	//To use in conjuction with the Evalvid Server pointer

namespace ns3  {

class MgtAddBaRequestHeader;

/**
 * \ingroup wifi
 *
 * The Wifi MAC high model for a non-AP STA in a BSS.
 */
class StaWifiMac : public RegularWifiMac
{
public:
  static TypeId GetTypeId (void);

  StaWifiMac ();
  virtual ~StaWifiMac ();

  /**
   * \param packet the packet to send.
   * \param to the address to which the packet should be sent.
   *
   * The packet should be enqueued in a tx queue, and should be
   * dequeued as soon as the channel access function determines that
   * access is granted to this MAC.
   */
  virtual void Enqueue (Ptr<const Packet> packet, Mac48Address to);

  /**
   * \param missed the number of beacons which must be missed
   * before a new association sequence is started.
   */
  void SetMaxMissedBeacons (uint32_t missed);
  /**
   * \param timeout
   *
   * If no probe response is received within the specified
   * timeout, the station sends a new probe request.
   */
  void SetProbeRequestTimeout (Time timeout);
  /**
   * \param timeout
   *
   * If no association response is received within the specified
   * timeout, the station sends a new association request.
   */
  void SetAssocRequestTimeout (Time timeout);

  /**
   * Start an active association sequence immediately.
   */
  void StartActiveAssociation (void);

/**
 * Added by Najwan
 * */
  void CheckPacketInfo (Ptr<Packet> packet_copy);
  
  //Setters and Getters
  void SetSourceIP(Ipv4Address SourceIP);
  void SetDestinationIP(Ipv4Address DestinationIP);
  void SetSourcePort(uint32_t Port);
  void SetSeqId(uint32_t SeqId);
  void SetFrameTypeValue(uint32_t FrameTypeValue);
  void SetFrameTypeString(std::string FrameType);
  void SetQosTid(uint8_t QosTid);
  void SetNodeId(uint32_t NodeId);
  void SetDevId(uint32_t DevId);
  void SetAppId(uint32_t AppId);
  Ipv4Address GetSourceIP();
  Ipv4Address GetDestinationIP();
  uint32_t GetSourcePort();
  uint32_t GetSeqId();
  uint32_t GetFrameTypeValue();
  std::string GetFrameTypeString();
  uint8_t GetQosTid();
  uint32_t GetNodeId();
  uint32_t GetDevId();
  uint32_t GetAppId();
  
  //Variables used in Setters and Getters
  Ipv4Address m_SourceIP, m_DestinationIP;
  uint32_t m_SourcePort, m_SeqId;
  int m_FrameTypeValue;
  std::string m_FrameTypeString;
  uint8_t m_QosTid;
  uint32_t m_NodeId;
  uint32_t m_AppId;
  uint32_t m_DevId;
  

  
  uint32_t m_qcase; /*queue case, which determines which queue to use*/
  uint32_t m_selectapp; /*select app that has the ability of the proposed algorithm*/
  uint32_t m_psnrthreshold; /*set the psnr threshold*/
  
  //CASES
  void IFrameToVoIfViFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address rceiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId);
  void IFrameToVoIfViFullVoLess50(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVo, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId);
  void PAndBFrameToBeIfViQueueMore80(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId);
  void RemoveAnyBFrameWithBDropBudgetForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue);
  void RemoveBToInsertI(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver);
  
  
  
  //Case 1
  void QueueDefault(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, uint32_t SourcePort, uint32_t AppId, std::string VidContentType, double TotalI, double TotalP, double TotalB);
  
  //Case 5
  void RemoveAnyBFrameForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId, uint32_t AppId);
    
  //Case 6
  void RemoveOwnBFrameForOwnIFrameIfViQueueFull(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, uint32_t SeqId, uint32_t AppId);
  
  //Case 8
  void RemoveAnyBFrameIfQoEBelow30(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId);
  
  //Case 9
  void RemoveOwnBFrameIfQoEBelow30(uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId);
  
  //Case 14
  void RemoveAnyBFrameHybrid (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId);
  
  //Case 15
  void RemoveOwnBFrameHybrid (uint8_t tid, Ptr<const Packet> packet, WifiMacHeader hdr, Ptr<WifiMacQueue> MacQueueNVi, Mac48Address receiver, Ptr<EvalvidServer> EvalServ, uint32_t SourcePort, uint32_t FrameTypeValue, double TotalI, double TotalP, double TotalB, std::string VidContentType, uint32_t SeqId, uint32_t AppId);
  
  void Log_ManualDrop(Ptr<const Packet> packet);
  
  bool FindDroppedPacketExist();
  void SetIDropCounter(uint32_t IDrop);
  void SetPDropCounter(uint32_t PDrop);
  void SetBDropCounter(uint32_t BDrop);
  uint32_t GetIDropCounter();
  uint32_t GetPDropCounter();
  uint32_t GetBDropCounter();
  
  
private:
  /**
   * The current MAC state of the STA.
   */
  enum MacState
  {
    ASSOCIATED,
    WAIT_PROBE_RESP,
    WAIT_ASSOC_RESP,
    BEACON_MISSED,
    REFUSED
  };

  /**
   * Enable or disable active probing.
   *
   * \param enable enable or disable active probing
   */
  void SetActiveProbing (bool enable);
  /**
   * Return whether active probing is enabled.
   *
   * \return true if active probing is enabled, false otherwise
   */
  bool GetActiveProbing (void) const;

  virtual void Receive (Ptr<Packet> packet, const WifiMacHeader *hdr);

  /**
   * Forward a probe request packet to the DCF. The standard is not clear on the correct
   * queue for management frames if QoS is supported. We always use the DCF.
   */
  void SendProbeRequest (void);
  /**
   * Forward an association request packet to the DCF. The standard is not clear on the correct
   * queue for management frames if QoS is supported. We always use the DCF.
   */
  void SendAssociationRequest (void);
  /**
   * Try to ensure that we are associated with an AP by taking an appropriate action
   * depending on the current association status.
   */
  void TryToEnsureAssociated (void);
  /**
   * This method is called after the association timeout occurred. We switch the state to
   * WAIT_ASSOC_RESP and re-send an association request.
   */
  void AssocRequestTimeout (void);
  /**
   * This method is called after the probe request timeout occurred. We switch the state to
   * WAIT_PROBE_RESP and re-send a probe request.
   */
  void ProbeRequestTimeout (void);
  /**
   * Return whether we are associated with an AP.
   *
   * \return true if we are associated with an AP, false otherwise
   */
  bool IsAssociated (void) const;
  /**
   * Return whether we are waiting for an association response from an AP.
   *
   * \return true if we are waiting for an association response from an AP, false otherwise
   */
  bool IsWaitAssocResp (void) const;
  /**
   * This method is called after we have not received a beacon from the AP
   */
  void MissedBeacons (void);
  /**
   * Restarts the beacon timer.
   *
   * \param delay the delay before the watchdog fires
   */
  void RestartBeaconWatchdog (Time delay);
  /**
   * Return an instance of SupportedRates that contains all rates that we support
   * including HT rates.
   *
   * \return SupportedRates all rates that we support
   */
  SupportedRates GetSupportedRates (void) const;
  /**
   * Set the current MAC state.
   *
   * \param value the new state
   */
  void SetState (enum MacState value);
  /**
   * Return the Capability information of the current STA.
   *
   * \return the Capability information that we support
   */
  CapabilityInformation GetCapabilities (void) const;

  enum MacState m_state;
  Time m_probeRequestTimeout;
  Time m_assocRequestTimeout;
  EventId m_probeRequestEvent;
  EventId m_assocRequestEvent;
  EventId m_beaconWatchdog;
  Time m_beaconWatchdogEnd;
  uint32_t m_maxMissedBeacons;
  bool m_activeProbing;
  
  //Variables used in Case 8
  uint32_t m_IDrop;	
  uint32_t m_PDrop;
  uint32_t m_BDrop;

  TracedCallback<Mac48Address> m_assocLogger;
  TracedCallback<Mac48Address> m_deAssocLogger;
};

} //namespace ns3

#endif /* STA_WIFI_MAC_H */

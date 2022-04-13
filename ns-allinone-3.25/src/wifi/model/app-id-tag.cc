/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 *	Added by Najwan to tag the app ID at Evalvid Server
 */
#include "app-id-tag.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (AppIdTag);

TypeId
AppIdTag::GetTypeId (void)
{
  static TypeId appid = TypeId ("ns3::AppIdTag")
    .SetParent<Tag> ()
    .AddConstructor<AppIdTag> ()
    .AddAttribute ("appid", "The appid that indicates AC which packet belongs",
                   UintegerValue (0),
                   MakeUintegerAccessor (&AppIdTag::GetAppId),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return appid;
}

TypeId
AppIdTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

AppIdTag::AppIdTag ()
  : m_appid (0)
{
}
AppIdTag::AppIdTag (uint32_t appid)
  : m_appid (appid)
{
}

void
AppIdTag::SetAppId (uint32_t appid)
{
  m_appid = appid;
}

//void
//AppIdTag::SetUserPriority (UserPriority up)
//{
  //m_aid = up;
//}

uint32_t
AppIdTag::GetSerializedSize (void) const
{
  return 1;
}

void
AppIdTag::Serialize (TagBuffer i) const
{
  i.WriteU8 (m_appid);
}

void
AppIdTag::Deserialize (TagBuffer i)
{
  //m_aid = (UserPriority) i.ReadU8 ();
  m_appid = i.ReadU8 ();
}

uint32_t
AppIdTag::GetAppId () const
{
  return m_appid;
}

void
AppIdTag::Print (std::ostream &os) const
{
  os << "AppId=" << m_appid;
}

} // namespace ns3

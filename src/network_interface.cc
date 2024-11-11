#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

static ARPMessage make_arp( const uint16_t opcode,
                            const EthernetAddress sender_ethernet_address,
                            const uint32_t sender_ip_address,
                            const EthernetAddress target_ethernet_address,
                            const uint32_t target_ip_address )
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = sender_ethernet_address;
  arp.sender_ip_address = sender_ip_address;
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = target_ip_address;
  return arp;
}

static EthernetFrame make_frame( const EthernetAddress& src,
                                 const EthernetAddress& dst,
                                 const uint16_t type,
                                 vector<string> payload )
{
  EthernetFrame frame;
  frame.header.src = src;
  frame.header.dst = dst;
  frame.header.type = type;
  frame.payload = std::move( payload );
  return frame;
}

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t target_ip_address = next_hop.ipv4_numeric();
  if ( ip_ether_map_.contains(
         target_ip_address ) ) { // If the destination Ethernet address is known, send it right away
    transmit( make_frame(
      ethernet_address_, ip_ether_map_[target_ip_address].first, EthernetHeader::TYPE_IPv4, serialize( dgram ) ) );
  } else { // If the destination Ethernet address is unknown, broadcast an ARP request
    datagrams_wait_sent_.push( std::make_pair( dgram, target_ip_address ) );
    if ( arp_time_.value_or( ARPINTERVAL ) >= ARPINTERVAL ) {
      transmit( make_frame(
        ethernet_address_,
        ETHERNET_BROADCAST,
        EthernetHeader::TYPE_ARP,
        serialize( make_arp(
          ARPMessage::OPCODE_REQUEST, ethernet_address_, ip_address_.ipv4_numeric(), {}, target_ip_address ) ) ) );
      arp_time_ = 0;
    }
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      datagrams_received_.push( std::move( dgram ) );
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    if ( parse( arp, frame.payload ) ) {
      ip_ether_map_.insert(
        std::make_pair( arp.sender_ip_address, std::make_pair( arp.sender_ethernet_address, 0 ) ) );
      if ( arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == ip_address_.ipv4_numeric() ) {
        transmit( make_frame( ethernet_address_,
                              arp.sender_ethernet_address,
                              EthernetHeader::TYPE_ARP,
                              serialize( make_arp( ARPMessage::OPCODE_REPLY,
                                                   ethernet_address_,
                                                   ip_address_.ipv4_numeric(),
                                                   arp.sender_ethernet_address,
                                                   arp.sender_ip_address ) ) ) );
      }

      while ( datagrams_wait_sent_.size() ) {
        uint32_t target_ip_address = datagrams_wait_sent_.front().second;
        if ( ip_ether_map_.contains( target_ip_address ) ) {
          transmit( make_frame( ethernet_address_,
                                ip_ether_map_[target_ip_address].first,
                                EthernetHeader::TYPE_IPv4,
                                serialize( datagrams_wait_sent_.front().first ) ) );
          datagrams_wait_sent_.pop();
          continue;
        }
        break;
      }
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  for ( auto it = ip_ether_map_.begin(); it != ip_ether_map_.end(); ) {
    it->second.second += ms_since_last_tick;
    if ( it->second.second >= LIVETIME ) {
      it = ip_ether_map_.erase( it );
    } else {
      ++it;
    }
  }

  if ( arp_time_.has_value() ) {
    arp_time_.value() += ms_since_last_tick;
  }
}

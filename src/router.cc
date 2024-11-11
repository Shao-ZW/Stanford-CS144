#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

static uint32_t get_net_mask( uint8_t prefix_length )
{
  return prefix_length == 0 ? 0 : 0xFFFFFFFF << ( 32 - prefix_length );
}

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routing_table_.push_back( std::make_tuple( route_prefix, prefix_length, next_hop, interface_num ) );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( auto _interface : _interfaces ) {
    auto& received_datagrams = _interface->datagrams_received();
    
    while ( received_datagrams.size() ) {
      InternetDatagram datagram { std::move( received_datagrams.front() ) };
      received_datagrams.pop();

      if ( datagram.header.ttl == 0 || datagram.header.ttl == 1 ) {
        continue;
      }

      datagram.header.ttl--;
      datagram.header.compute_checksum();

      uint8_t plength = 0;
      size_t interface_id = -1;
      std::optional<Address> next_hog {};

      for ( auto& entry : routing_table_ ) {
        uint32_t route_prefix = std::get<0>( entry );
        uint8_t prefix_length = std::get<1>( entry );
        if ( route_prefix == ( datagram.header.dst & get_net_mask( prefix_length ) ) ) {
          if ( plength == 0 || prefix_length > plength ) {
            plength = prefix_length;
            next_hog = std::get<2>( entry ).value_or( Address::from_ipv4_numeric( datagram.header.dst ) );
            interface_id = std::get<3>( entry );
          }
        }
      }

      if ( next_hog.has_value() ) {
        interface( interface_id )->send_datagram( datagram, next_hog.value() );
      }
    }
  }
}

#include "router.hh"

#include <bit>
#include <cstddef>
#include <iostream>
#include <optional>
#include <ranges>

using namespace std;

void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routable[prefix_length][rotr( route_prefix, 32 - prefix_length )] = { interface_num, next_hop };
}

void Router::route()
{
  for ( const auto& interface : _interfaces ) {
    auto&& get_datagrams = interface->datagrams_received();
    while ( not get_datagrams.empty() ) {
      InternetDatagram now_datagram = move( get_datagrams.front() );
      get_datagrams.pop();

      if ( now_datagram.header.ttl <= 1 ) {
        continue;
      }
      now_datagram.header.ttl -= 1;
      now_datagram.header.compute_checksum();

      uint32_t addr = now_datagram.header.dst;
      auto adap = views::filter( [&addr]( const auto& mp ) { return mp.contains( addr >>= 1 ); } )
                  | views::transform( [&addr]( const auto& mp ) -> meg { return mp.at( addr ); } );
      auto re = routable | views::reverse | adap | views::take( 1 );

      optional<meg> right_info = re.empty() ? nullopt : optional<meg> { re.front() };

      if ( not right_info.has_value() ) {
        continue;
      }

      const auto& [num, next_hop] = right_info.value();
      _interfaces[num]->send_datagram( now_datagram,
                                       next_hop.value_or( Address::from_ipv4_numeric( now_datagram.header.dst ) ) );
    }
  }
}

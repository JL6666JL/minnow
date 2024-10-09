#include "reassembler.hh"

#include <algorithm>
#include <ranges>

using namespace std;

auto Reassembler::split( uint64_t position )
{
  auto lowerBoundIt = databuf.lower_bound( position );
  if ( lowerBoundIt != databuf.end() && lowerBoundIt->first == position ) {
    return lowerBoundIt;
  }
  if ( lowerBoundIt == databuf.begin() ) {
    return lowerBoundIt;
  }
  auto previousIt = prev( lowerBoundIt );
  if ( previousIt->first + previousIt->second.size() > position ) {
    auto newEntry
      = databuf.emplace_hint( lowerBoundIt, position, previousIt->second.substr( position - previousIt->first ) );
    previousIt->second.resize( position - previousIt->first );
    return newEntry;
  }
  return lowerBoundIt;
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( data.empty() ) {
    if ( !end_pos.has_value() && is_last_substring ) {
      end_pos.emplace( first_index );
    }
    if ( end_pos.has_value() && end_pos.value() == writer().bytes_pushed() ) {
      output_.writer().close();
    }
    return;
  }

  if ( writer().is_closed() || writer().available_capacity() == 0U ) {
    return;
  }

  uint64_t pushedBytes = writer().bytes_pushed();
  uint64_t capacityLimit = pushedBytes + writer().available_capacity();

  if ( first_index + data.size() <= pushedBytes || first_index >= capacityLimit ) {
    return;
  }

  if ( first_index + data.size() > capacityLimit ) {
    data.resize( capacityLimit - first_index );
    is_last_substring = false;
  }

  if ( first_index < pushedBytes ) {
    data.erase( 0, pushedBytes - first_index );
    first_index = pushedBytes;
  }

  if ( !end_pos.has_value() && is_last_substring ) {
    end_pos.emplace( first_index + data.size() );
  }

  auto upperBound = split( first_index + data.size() );
  auto lowerBound = split( first_index );
  for ( const auto& str : ranges::subrange( lowerBound, upperBound ) | views::values ) {
    pending_num -= str.size();
  }
  pending_num += data.size();
  databuf.emplace_hint( databuf.erase( lowerBound, upperBound ), first_index, move( data ) );

  while ( !databuf.empty() ) {
    auto& [index, payload] = *databuf.begin();
    if ( index != writer().bytes_pushed() ) {
      break;
    }

    pending_num -= payload.size();
    output_.writer().push( move( payload ) );
    databuf.erase( databuf.begin() );
  }

  if ( end_pos.has_value() && end_pos.value() == writer().bytes_pushed() ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return pending_num;
}
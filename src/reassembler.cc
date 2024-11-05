#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( is_last_substring ) {
    finish_index_ = first_index + data.size();
  }

  if ( unassembled_index_ > first_index + data.size() ) {
    return;
  }

  uint64_t available_first_index = max( first_index, unassembled_index_ );
  uint64_t available_last_index
    = min( first_index + data.size(), unassembled_index_ + output_.writer().available_capacity() );
  uint64_t new_left_index = available_first_index;
  uint64_t new_right_index = available_last_index;
  auto left_iter = buf_.begin();
  auto right_iter = buf_.rbegin();
  std::string merge_data = "";

  while ( left_iter != buf_.end() ) {
    uint64_t left_index = std::get<0>( *left_iter );
    uint64_t right_index = std::get<1>( *left_iter );
    if ( available_first_index >= left_index && available_first_index <= right_index ) {
      merge_data += std::get<2>( *left_iter ).substr( 0, available_first_index - left_index );
      new_left_index = left_index;
      break;
    } else if ( available_first_index < left_index ) {
      new_left_index = available_first_index;
      break;
    }
    ++left_iter;
  }

  merge_data += data.substr( available_first_index - first_index, available_last_index - first_index );

  while ( right_iter != buf_.rend() ) {
    uint64_t left_index = std::get<0>( *right_iter );
    uint64_t right_index = std::get<1>( *right_iter );
    if ( available_last_index >= left_index && available_last_index <= right_index ) {
      merge_data += std::get<2>( *right_iter ).substr( available_last_index - left_index );
      new_right_index = right_index;
      break;
    } else if ( available_last_index > right_index ) {
      new_right_index = available_last_index;
      break;
    }
    ++right_iter;
  }

  pending_bytes_ += merge_data.size();
  while ( left_iter != right_iter.base() ) {
    pending_bytes_ -= std::get<2>( *left_iter ).size();
    left_iter = buf_.erase( left_iter );
  }

  buf_.insert( right_iter.base(), std::make_tuple( new_left_index, new_right_index, std::move( merge_data ) ) );

  if ( unassembled_index_ == std::get<0>( *buf_.begin() ) ) {
    unassembled_index_ = std::get<1>( *buf_.begin() );
    pending_bytes_ -= std::get<2>( *buf_.begin() ).size();
    output_.writer().push( std::move( std::get<2>( *buf_.begin() ) ) );
    buf_.pop_front();

    if ( unassembled_index_ >= finish_index_ ) {
      output_.writer().close();
    }
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return pending_bytes_;
}

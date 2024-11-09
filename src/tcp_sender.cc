#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return seqno_ - ackno_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmission_cnts_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  if ( status_ == Status::SYNSENT || status_ == Status::FINSENT || status_ == Status::FINISHED ) {
    return;
  }

  uint16_t window_size = ( window_size_ == 0 ) ? 1 : window_size_;

  while ( ( status_ == Status::CLOSED || status_ == Status::ESTABLISHED )
          && window_size > sequence_numbers_in_flight() ) {
    std::string_view input = input_.reader().peek();
    std::string payload = "";
    bool syn = ( status_ == Status::CLOSED );

    if ( !syn && seqno_ - ackno_ < input.size() ) {
      std::string_view data = input.substr( seqno_ - ackno_ );
      size_t payload_size
        = min( TCPConfig::MAX_PAYLOAD_SIZE,
               min( data.size(), static_cast<size_t>( window_size - syn - sequence_numbers_in_flight() ) ) );
      payload = data.substr( 0, payload_size );
    }

    bool fin = input_.writer().is_closed() && seqno_ + payload.size() == ackno_ + input.size()
               && ( window_size > sequence_numbers_in_flight() + syn + payload.size() );
    TCPSenderMessage tcp_sender_message
      = { Wrap32::wrap( seqno_, isn_ ), syn, std::move( payload ), fin, input_.reader().has_error() };

    if ( tcp_sender_message.sequence_length() == 0 ) {
      return;
    }

    if ( tcp_sender_message.SYN ) {
      status_ = Status::SYNSENT;
    } else if ( tcp_sender_message.FIN ) {
      status_ = Status::FINSENT;
    }
    outstanding_.push_back( seqno_ );
    seqno_ = seqno_ + tcp_sender_message.sequence_length();
    transmit( tcp_sender_message );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { Wrap32::wrap( seqno_, isn_ ), false, "", false, input_.reader().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( status_ == Status::FINISHED ) {
    return;
  }

  if ( msg.RST ) {
    input_.reader().set_error();
  }

  uint64_t msg_ackno = msg.ackno->unwrap( isn_, seqno_ );

  if ( msg_ackno > ackno_ && msg_ackno <= seqno_ ) {
    rto_ = initial_RTO_ms_;
    retransmission_timer_ = 0;
    retransmission_cnts_ = 0;

    if ( msg_ackno == seqno_ ) {
      outstanding_.clear();

      if ( status_ != Status::SYNSENT ) {
        input_.reader().pop( msg_ackno - ackno_ );
      }
      ackno_ = msg_ackno;

      if ( status_ == Status::FINSENT ) {
        status_ = Status::FINISHED;
      } else if ( status_ == Status::SYNSENT ) {
        status_ = Status::ESTABLISHED;
      }
    } else {
      auto it = std::upper_bound( outstanding_.begin(), outstanding_.end(), msg_ackno );
      outstanding_.erase( outstanding_.begin(), it - 1 );
      msg_ackno = *( it - 1 );
      input_.reader().pop( msg_ackno - ackno_ );
      ackno_ = msg_ackno;
    }
  }

  window_size_ = msg.window_size;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( status_ == Status::FINISHED ) {
    return;
  }

  retransmission_timer_ += ms_since_last_tick;

  if ( retransmission_timer_ >= rto_ ) {
    std::string_view input = input_.reader().peek();
    uint16_t window_size = ( window_size_ == 0 ) ? 1 : window_size_;
    size_t payload_size = outstanding_.size() > 1 ? outstanding_[1] - outstanding_[0]
                                                  : min( TCPConfig::MAX_PAYLOAD_SIZE,
                                                         min( input.size(), static_cast<size_t>( window_size ) ) );
    std::string payload( input.substr( 0, payload_size ) );
    bool syn = ( ackno_ == 0 );
    bool fin
      = status_ == Status::FINSENT && ackno_ + payload.size() + 1 == seqno_ && window_size > syn + payload.size();

    TCPSenderMessage tcp_sender_message
      = { Wrap32::wrap( ackno_, isn_ ), syn, std::move( payload ), fin, input_.reader().has_error() };

    if ( window_size_ != 0 ) {
      rto_ *= 2;
    }

    retransmission_cnts_ += 1;
    retransmission_timer_ = 0;

    if ( tcp_sender_message.sequence_length() ) {
      transmit( tcp_sender_message );
    }
  }
}

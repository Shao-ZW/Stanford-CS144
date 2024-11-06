#include "tcp_receiver.hh"

#include <limits>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( message.SYN ) {
    ISN_ = message.seqno;
    reassembler_.insert( 0, message.payload, message.FIN );
    return;
  }

  if ( ISN_.has_value() ) {
    uint64_t abs_seqno = message.seqno.unwrap( ISN_.value(), reassembler_.writer().bytes_pushed() );
    if ( abs_seqno > 0 ) {
      reassembler_.insert( abs_seqno - 1, message.payload, message.FIN );
    }
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  return TCPReceiverMessage {
    ISN_ == nullopt
      ? nullopt
      : std::make_optional<Wrap32>( Wrap32::wrap(
        reassembler_.writer().bytes_pushed() + 1 + reassembler_.writer().is_closed(), ISN_.value() ) ),
    static_cast<uint16_t>( min( reassembler_.writer().available_capacity(), static_cast<uint64_t>( UINT16_MAX ) ) ),
    reassembler_.writer().has_error() };
}

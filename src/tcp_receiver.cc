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
    isn_ = message.seqno;
    is_connected_ = true;
    reassembler_.insert( 0, message.payload, message.FIN );
    return;
  }

  if ( is_connected_ ) {
    uint64_t abs_seqno = message.seqno.unwrap( isn_, reassembler_.writer().bytes_pushed() );
    if ( abs_seqno > 0 ) {
      reassembler_.insert( abs_seqno - 1, message.payload, message.FIN );
    }
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  return TCPReceiverMessage {
    !is_connected_ ? nullopt
                   : std::make_optional<Wrap32>( Wrap32::wrap(
                     reassembler_.writer().bytes_pushed() + 1 + reassembler_.writer().is_closed(), isn_ ) ),
    static_cast<uint16_t>( min( reassembler_.writer().available_capacity(), static_cast<uint64_t>( UINT16_MAX ) ) ),
    reassembler_.writer().has_error() };
}

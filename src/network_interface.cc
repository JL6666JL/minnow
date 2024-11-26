#include <iostream>

#include "arp_message.hh"
#include "exception.hh"
#include "network_interface.hh"

using namespace std;

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
void NetworkInterface::send_datagram(const InternetDatagram& dgram, const Address& next_hop) {
    IPv4NumericAddress next_hop_num = next_hop.ipv4_numeric();
    
    auto now_entry = arp_table_.find(next_hop_num);
    if (now_entry != arp_table_.end()) {
        const EthernetAddress& dst = now_entry->second.first;
        EthernetFrame frame;
        frame.header.dst = dst;
        frame.header.src = ethernet_address_;
        frame.header.type = EthernetHeader::TYPE_IPv4;
        frame.payload = serialize(dgram);
        transmit(frame);
        return;
    }

    pending_datagrams_[next_hop_num].push_back(dgram);

    if (pending_timers_.find(next_hop_num) != pending_timers_.end()) {
        return;
    }

    pending_timers_[next_hop_num] = NetworkInterface::TimeoutTracker();

    ARPMessage arp_request;
    arp_request.opcode = ARPMessage::OPCODE_REQUEST;
    arp_request.sender_ethernet_address = ethernet_address_;
    arp_request.sender_ip_address = ip_address_.ipv4_numeric();
    arp_request.target_ethernet_address = {};
    arp_request.target_ip_address = next_hop_num;

    EthernetFrame arp_frame;
    arp_frame.header.dst = ETHERNET_BROADCAST;
    arp_frame.header.src = ethernet_address_;
    arp_frame.header.type = EthernetHeader::TYPE_ARP;
    arp_frame.payload = serialize(arp_request);

    transmit(arp_frame);
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame(const EthernetFrame& frame) {
    if (frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST) {
        return;
    }

    switch (frame.header.type) {
        case EthernetHeader::TYPE_IPv4: {
            InternetDatagram ipv4_data;
            if (parse(ipv4_data, frame.payload)) {
                datagrams_received_.push(std::move(ipv4_data));
            }
            break;
        }
        case EthernetHeader::TYPE_ARP: {
            ARPMessage message;
            if (!parse(message, frame.payload)) {
                return;
            }

            arp_table_[message.sender_ip_address] = {message.sender_ethernet_address, TimeoutTracker()};

            if (message.opcode == ARPMessage::OPCODE_REQUEST &&
                message.target_ip_address == ip_address_.ipv4_numeric()) {
                ARPMessage arp_reply;
                arp_reply.opcode = ARPMessage::OPCODE_REPLY;
                arp_reply.sender_ethernet_address = ethernet_address_;
                arp_reply.sender_ip_address = ip_address_.ipv4_numeric();
                arp_reply.target_ethernet_address = message.sender_ethernet_address;
                arp_reply.target_ip_address = message.sender_ip_address;

                EthernetFrame arp_frame;
                arp_frame.header.dst = message.sender_ethernet_address;
                arp_frame.header.src = ethernet_address_;
                arp_frame.header.type = EthernetHeader::TYPE_ARP;
                arp_frame.payload = serialize(arp_reply);

                transmit(arp_frame);
            }

            auto pending_it = pending_datagrams_.find(message.sender_ip_address);
            if (pending_it != pending_datagrams_.end()) {
                for (const auto& dgram : pending_it->second) {
                    EthernetFrame ipv4_frame;
                    ipv4_frame.header.dst = message.sender_ethernet_address;
                    ipv4_frame.header.src = ethernet_address_;
                    ipv4_frame.header.type = EthernetHeader::TYPE_IPv4;
                    ipv4_frame.payload = serialize(dgram);
                    transmit(ipv4_frame);
                }
                pending_datagrams_.erase(pending_it);
                pending_timers_.erase(message.sender_ip_address);
            }
            break;
        }
        default:
            break;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    std::vector<IPv4NumericAddress> exarp;
    for (auto& entry : arp_table_) {
        if (entry.second.second.tick(ms_since_last_tick).expired(ARP_CACHE_ENTRY_LIFETIME)) {
            exarp.push_back(entry.first);
        }
    }
    for (const auto& address : exarp) {
        arp_table_.erase(address);
    }

    std::vector<IPv4NumericAddress> extimers;
    for (auto& entry : pending_timers_) {
        if (entry.second.tick(ms_since_last_tick).expired(ARP_RESPONSE_TTL)) {
            extimers.push_back(entry.first);
        }
    }
    for (const auto& address : extimers) {
        pending_timers_.erase(address);
    }
}

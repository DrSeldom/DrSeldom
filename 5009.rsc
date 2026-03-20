# 2026-03-20 15:31:14 by RouterOS 7.22
# software id = Q7A7-G5FS
#
# model = RB5009UG+S+
# serial number = HJY0AH3NQQG
/interface bridge
add name=bridge1
/interface ethernet
set [ find default-name=ether1 ] l2mtu=1514
set [ find default-name=ether2 ] l2mtu=1514
set [ find default-name=ether3 ] l2mtu=1514
set [ find default-name=ether4 ] l2mtu=1514
set [ find default-name=ether5 ] l2mtu=1514
set [ find default-name=ether6 ] l2mtu=1514
set [ find default-name=ether7 ] l2mtu=1514
set [ find default-name=ether8 ] l2mtu=1514
set [ find default-name=sfp-sfpplus1 ] l2mtu=1514
/interface wireguard
add listen-port=36860 mtu=1420 name=wg1
/interface list
add name=WAN
add name=LAN
/interface bridge port
add bridge=bridge1 interface=ether1
add bridge=bridge1 interface=ether2
add bridge=bridge1 interface=ether3
add bridge=bridge1 interface=ether4
add bridge=bridge1 interface=ether5
add bridge=bridge1 interface=ether6
add bridge=bridge1 interface=ether7
add bridge=bridge1 interface=ether8
add bridge=bridge1 interface=sfp-sfpplus1
/ip neighbor discovery-settings
set discover-interface-list=!dynamic
/interface list member
add interface=ether1 list=WAN
add interface=ether2 list=LAN
add interface=ether3 list=LAN
add interface=ether4 list=LAN
add interface=ether5 list=LAN
add interface=ether6 list=LAN
add interface=ether7 list=LAN
add interface=ether8 list=LAN
add interface=sfp-sfpplus1 list=LAN
/interface wireguard peers
add allowed-address=0.0.0.0/0 client-address=10.0.0.6/24 \
    client-allowed-address=::/0 endpoint-address=204.168.139.164 \
    endpoint-port=51820 interface=wg1 name=peer2 persistent-keepalive=25s \
    public-key="hRCYk80wQeDSNkFgTULefojKj8VawxYGXOdZl8Lh4hY="
/ip dhcp-client
add interface=bridge1 name=client1
/system clock
set time-zone-name=Europe/Minsk

# 2026-03-20 16:36:17 by RouterOS 7.22
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
add bridge=bridge1 disabled=yes interface=ether1
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
add interface=bridge1 list=LAN
add interface=wg1 list=WAN
/interface wireguard peers
add allowed-address=0.0.0.0/0 client-address=10.0.0.6/24 \
    client-allowed-address=::/0 endpoint-address=204.168.139.164 \
    endpoint-port=51820 interface=wg1 name=peer2 persistent-keepalive=25s \
    public-key="hRCYk80wQeDSNkFgTULefojKj8VawxYGXOdZl8Lh4hY="
/ip address
add address=192.168.0.81/24 interface=bridge1 network=192.168.0.0
add address=10.0.0.6/24 interface=wg1 network=10.0.0.0
/ip dhcp-client
add disabled=yes interface=ether1 name=client1
/ip dns
set servers=1.1.1.1,8.8.8.8
/ip firewall filter
add action=accept chain=input comment="Allow LAN" connection-state=\
    established,related,new,untracked src-address=192.168.0.0/24
add action=accept chain=input comment="Allow LAN" connection-state=\
    established,related,new,untracked src-address=192.168.0.0/24
add action=drop chain=input comment="Drop winbox/dude from WAN" content=\
    "denied winbox/dude connect from" in-interface-list=WAN
add action=add-dst-to-address-list address-list=bl_pz address-list-timeout=\
    none-dynamic chain=input in-interface-list=WAN log=yes protocol=tcp \
    tcp-flags=syn,rst
add action=add-dst-to-address-list address-list=bl_pz address-list-timeout=\
    1d7h55m44s chain=input dst-address-list=!dns in-interface-list=WAN \
    protocol=tcp psd=21,3s,3,1
add action=add-src-to-address-list address-list=bl_pz address-list-timeout=\
    1h55m44s chain=input in-interface-list=WAN protocol=tcp tcp-flags=\
    fin,syn,rst,psh,ack,urg
add action=drop chain=input in-interface-list=WAN protocol=tcp src-port=8291
add action=drop chain=input connection-state=new protocol=tcp src-port=\
    20-25,81,110,161,2000,445,3128,3306,53,3389,7547,8291,8080-8082
add action=drop chain=input in-interface-list=WAN protocol=tcp psd=21,3s,3,1
add action=accept chain=forward connection-state=\
    established,related,untracked src-address=192.168.0.0/24
add action=accept chain=input protocol=udp src-port=1813
add action=drop chain=forward connection-nat-state=!dstnat connection-state=\
    invalid
/ip firewall nat
add action=masquerade chain=srcnat comment="NAT via WAN" out-interface=ether1
add action=masquerade chain=srcnat comment="NAT via WireGuard" out-interface=\
    wg1
/ip firewall raw
add action=add-dst-to-address-list address-list=bl_pz address-list-timeout=\
    14w1d7h10m chain=output content="invalid user name or password" log=yes \
    log-prefix=AAAA>
add action=drop chain=prerouting in-interface-list=WAN log=yes log-prefix=\
    "bh >. " src-address-list=BH
add action=drop chain=prerouting in-interface-list=WAN log=yes log-prefix=\
    "bl_pz> " src-address-list=bl_pz
add action=drop chain=prerouting dst-port=53 in-interface-list=WAN protocol=\
    tcp
add action=drop chain=prerouting dst-port=53 in-interface-list=WAN log=yes \
    protocol=udp
add action=drop chain=prerouting in-interface-list=WAN protocol=tcp src-port=\
    23,8291,9001,10001,2000
add action=drop chain=prerouting comment="Fragments drop" fragment=yes \
    in-interface-list=WAN
/ip firewall service-port
set ftp disabled=yes
/ip route
add comment="WAN default" distance=1 dst-address=0.0.0.0/0 gateway=ether1 \
    routing-table=main
add comment="WG endpoint via WAN" distance=1 dst-address=204.168.139.164/32 \
    gateway=ether1 routing-table=main
add comment="bypass WG: 222.1.1.1" distance=1 dst-address=222.1.1.1/32 \
    gateway=ether1 routing-table=main
add comment="bypass WG: 133.3.4.1" distance=1 dst-address=133.3.4.1/32 \
    gateway=ether1 routing-table=main
/ip service
set ftp disabled=yes
set ssh disabled=yes
set telnet address=192.168.0.4/32
set www disabled=yes
set winbox address=192.168.0.0/24
set api address=192.168.0.0/24
set api-ssl address=192.168.0.0/24
/system clock
set time-zone-name=Europe/Minsk
/system identity
set name=RB5009Ma
/system ntp client
set enabled=yes
/system ntp client servers
add address=194.190.168.1
/system scheduler
add interval=1d name=schedule1 on-event=\
    "/system backup save name=[/system clock get date]" policy=\
    read,write,policy,test start-date=2026-03-20 start-time=00:00:25

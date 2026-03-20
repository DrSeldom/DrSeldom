# 2026-03-20 16:04:53 by RouterOS 7.22
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
/ip pool
add name=dhcp ranges=192.168.0.3-192.168.0.254
/queue simple
add max-limit=45M/45M name=queue1 target=192.168.0.0/24
/system script
add dont-require-permissions=no name=script1 owner=admin policy=\
    read,write,policy,test source="#  Spamhaus DROP (address-list)\
    \n:local url \"https://www.spamhaus.org/drop/drop.txt\"\
    \n:local listName \"spamhaus_blackhole\"\
    \n:local listNamee \"BH\"\
    \n:foreach i in=[/ip firewall address-list find where list=\$listName] do=\
    {\
    \n    /ip firewall address-list remove \$i\
    \n    :log info \"Removed old address-list entry: \$i\"\
    \n}\
    \n:foreach i in=[/ip firewall address-list find where list=\$listNamee] do\
    ={\
    \n    /ip firewall address-list remove \$i\
    \n    :log info \"Removed old address-list entry: \$i\"\
    \n}\
    \n:do {\
    \n    /tool fetch url=\$url mode=https dst-path=spamhaus_drop.txt\
    \n} on-error={\
    \n    :log error \"Failed to download Spamhaus DROP list\"\
    \n    :error \"Download failed\"\
    \n}\
    \n:if ([:len [/file find name=spamhaus_drop.txt]] = 0) do={\
    \n    :log error \"Spamhaus DROP file not found\"\
    \n    :error \"File not found\"\
    \n}\
    \n:local fileSize [/file get spamhaus_drop.txt size]\
    \n:if (\$fileSize < 100) do={\
    \n    :log error \"Spamhaus DROP file too small, possibly incomplete\"\
    \n    :error \"File too small\"\
    \n}\
    \n:local fileContent [/file get spamhaus_drop.txt contents]\
    \n:local start 0\
    \n:local len [:len \$fileContent]\
    \n:while (\$start < \$len) do={\
    \n    :local end [:find \$fileContent \"\\n\" \$start]\
    \n    :if ([:typeof \$end] = \"nil\") do={ :set end \$len }\
    \n    :local line [:pick \$fileContent \$start \$end]\
    \n    :set start (\$end + 1)\
    \n    :local lineLen [:len \$line]\
    \n    :if (\$lineLen > 0 && [:pick \$line (\$lineLen - 1) \$lineLen] = \"\
    \\r\") do={\
    \n        :set line [:pick \$line 0 (\$lineLen - 1)]\
    \n        :set lineLen (\$lineLen - 1)\
    \n    }\
    \n    :if (\$lineLen > 0) do={\
    \n        :if ([:pick \$line 0 1] != \";\") do={\
    \n            :local spacePos [:find \$line \" \"]\
    \n            :local semiPos  [:find \$line \";\"]\
    \n            :local sepPos \$lineLen\
    \n            :if ([:typeof \$spacePos] != \"nil\" && \$spacePos < \$sepPo\
    s) do={ :set sepPos \$spacePos }\
    \n            :if ([:typeof \$semiPos]  != \"nil\" && \$semiPos  < \$sepPo\
    s) do={ :set sepPos \$semiPos }\
    \n            :local cidr [:pick \$line 0 \$sepPos]\
    \n            :if ([:len \$cidr] > 0 && [:typeof [:find \$cidr \"/\"]] != \
    \"nil\") do={\
    \n                :do {\
    \n                    /ip firewall address-list add address=\$cidr list=\$\
    listNamee\
    \n                    :log info \"Added Spamhaus address: \$cidr\"\
    \n                } on-error={\
    \n                    :log warning \"Failed to add address \$cidr\"\
    \n                }\
    \n            } else={\
    \n                :log warning \"Skipped invalid line: \$line (cidr: \$cid\
    r)\"\
    \n            }\
    \n        }\
    \n    }\
    \n}\
    \n:do {\
    \n    /file remove spamhaus_drop.txt\
    \n} on-error={\
    \n    :log warning \"Failed to remove temp file\"\
    \n}\
    \n:log info \"Spamhaus DROP address-list updated successfully\""
/interface bridge port
add bridge=*E interface=ether2
add bridge=*E interface=ether3
add bridge=*E interface=ether4
add bridge=*E interface=ether5
add bridge=*E interface=ether6
add bridge=*E interface=ether7
add bridge=*E interface=ether8
add bridge=*E interface=sfp-sfpplus1
/ip neighbor discovery-settings
set discover-interface-list=!dynamic
/interface list member
add interface=ether1 list=WAN
add interface=bridge1 list=LAN
add interface=wg1 list=WAN
add interface=*E list=LAN
/interface wireguard peers
add allowed-address=0.0.0.0/0 client-address=10.0.0.6/24 \
    client-allowed-address=::/0 endpoint-address=204.168.139.164 \
    endpoint-port=51820 interface=wg1 name=peer2 persistent-keepalive=25s \
    public-key="hRCYk80wQeDSNkFgTULefojKj8VawxYGXOdZl8Lh4hY="
/ip address
add address=82.209.233.130/30 interface=ether1 network=82.209.233.128
add address=192.168.0.1/24 interface=ether2 network=192.168.0.0
add address=10.0.0.6/24 interface=wg1 network=10.0.0.0
/ip dhcp-client
add disabled=yes interface=ether1 name=client1
/ip dhcp-server
add address-pool=dhcp disabled=yes interface=ether2 name=dhcp1
/ip dhcp-server network
add address=192.168.0.0/24 dns-server=192.168.0.1 gateway=192.168.0.1 \
    netmask=24
/ip dns
set servers=1.1.1.1,82.209.240.241,8.8.8.8,82.209.243.241
/ip firewall address-list
add address=146.88.0.0/16 list=bl_pz
add address=87.241.219.0/24 list=bl_pz
add address=146.88.241.0/24 list=bl_pz
add address=82.209.233.0/24 disabled=yes list=bl_pz
add address=82.209.233.129 list=dns
add address=82.209.233.130 list=dns
add address=81.8.93.66 list=bl_pz
add address=81.8.93.0/24 list=bl_pz
add address=146.88.240.0/24 list=bl_pz
add address=217.65.208.0/20 list=bl_pz
add address=77.51.216.0/21 list=bl_pz
add address=152.32.149.0/24 list=bl_pz
add address=44.192.0.0/11 list=bl_pz
add address=46.166.96.0/19 list=bl_pz
add address=194.124.36.0/24 list=bl_pz
add address=44.192.0.0/10 list=bl_pz
add address=62.37.0.0/16 list=bl_pz
add address=87.236.176.54 list=bl_pz
add address=45.55.151.3 list=bl_pz
add address=37.46.119.0/24 comment=1 disabled=yes list=bl_pz
add address=3.149.59.0/24 list=bl_pz
add address=78.29.8.0/21 list=bl_pz
add address=3.137.73.0/24 list=bl_pz
add address=162.142.125.0/24 list=bl_pz
add address=66.132.153.0/24 list=bl_pz
add address=167.94.138.0/24 list=bl_pz
add address=91.230.168.0/24 list=bl_pz
add address=118.193.45.0/24 list=bl_pz
add address=199.45.154.0/23 list=bl_pz
add address=206.191.152.0/21 list=bl_pz
add address=193.124.182.0/24 list=bl_pz
add address=83.237.0.0/16 list=bl_pz
add address=91.211.182.0/24 list=bl_pz
/ip firewall filter
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
add action=accept chain=input protocol=udp src-port=1813
add action=accept chain=forward comment="Allow LAN new connections" \
    connection-state=new src-address=192.168.0.0/24
add action=accept chain=forward comment="Allow established/related" \
    connection-state=established,related,untracked
add action=drop chain=forward comment="Drop invalid" connection-state=invalid
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
add comment="WG endpoint via WAN" distance=1 dst-address=204.168.139.164/32 \
    gateway=82.209.233.129 routing-table=main
add comment="bypass WG: 222.1.1.1" distance=1 dst-address=222.1.1.1/32 \
    gateway=82.209.233.129 routing-table=main
add comment="bypass WG: 133.3.4.1" distance=1 dst-address=133.3.4.1/32 \
    gateway=82.209.233.129 routing-table=main
add comment="Default via WireGuard" distance=1 dst-address=0.0.0.0/0 gateway=\
    wg1 routing-table=main
/ip service
set ftp disabled=yes
set ssh disabled=yes
set telnet address=192.168.0.4/32
set www disabled=yes
set winbox address=192.168.0.0/24
set api address=192.168.0.0/24
set api-ssl address=192.168.0.0/24
/ppp aaa
set use-radius=yes
/radius
add address=192.168.0.71 disabled=yes domain=gurmina.local service=ppp,login
/system clock
set time-zone-name=Europe/Amsterdam
/system identity
set name=RB5009Ma
/system ntp client
set enabled=yes
/system ntp client servers
add address=194.190.168.1
/system package update
set channel=testing
/system scheduler
add interval=1d name=schedule1 on-event=\
    "/system backup save name=[/system clock get date]" policy=\
    read,write,policy,test start-date=2026-03-20 start-time=00:00:25

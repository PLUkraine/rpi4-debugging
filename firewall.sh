sudo firewall-cmd  --add-service=nfs
sudo firewall-cmd  --add-service=rpc-bind
sudo firewall-cmd  --add-service=mountd
sudo firewall-cmd  --add-service=tftp
sudo firewall-cmd --list-services
# sudo firewall-cmd --reload

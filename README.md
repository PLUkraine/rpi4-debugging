# RPI4 Debugging Lab

This is an external Buildroot layer to be used with Raspberry Pi 4B for Bootlin's Debugging Lab. 
Bootlin does not officially support Raspberry Pi 4B, but it is possible to adapt the lab setup. 
We take Buildroot 2026.02.x branch and patch it to setup netboot via U-Boot and NFS rootfs.

## Prerequisites

### Fedora

```bash
sudo dnf install \
    which sed make binutils gcc gcc-c++ bash patch gzip \
    bzip2 tar perl rsync file findutils python3 unzip wget git \
    picocom
```

### Debian/Ubuntu

```
sudo apt update && sudo apt install -y \
    build-essential bash bc binutils bzip2 cpio diffutils file \
    g++ gcc gzip make patch perl rsync sed tar unzip wget \
    git libncurses5-dev python3-dev \
    picocom
```

## Build an Image

Run these commands in your terminal:

```bash
git clone https://github.com/PLUkraine/rpi4-debugging.git
git clone https://github.com/buildroot/buildroot.git
cd buildroot
git checkout 2026.02.x
make BR2_EXTERNAL=../rpi4-debugging raspberrypi4_64_defconfig
make
```

This will build the netbootable image at `output/images/sdcard.img`, but does not enable netboot. 
You need to setup TFTP, NFS and U-Boot on the host machine.

## Setup NFS

### Fedora

```bash
# install NFS
sudo dnf install nfs-utils

# unpack Buildroot rootfs (ext4 filesystem image) into NFS root
sudo mkdir -p /mnt/rpi-rootfs
sudo mount -o loop output/images/rootfs.ext2 /mnt/rpi-rootfs
sudo cp -a /mnt/rpi-rootfs/. /srv/nfs/rpi4-root
sudo umount /mnt/rpi-rootfs
# make rootfs discoverable by RPI4
echo '/srv/nfs/rpi4-root *(rw,sync,no_subtree_check,no_root_squash)' | sudo tee -a /etc/exports

# enable the service
sudo systemctl enable --now nfs-server
sudo exportfs -ra
# verify the config applied
sudo exportfs

# enable firewall rules
sudo firewall-cmd --permanent --add-service=nfs
sudo firewall-cmd --permanent --add-service=rpc-bind
sudo firewall-cmd --permanent --add-service=mountd
sudo firewall-cmd --reload
```

### Debian/Ubuntu (untested!)

```bash
# install NFS
sudo apt install nfs-kernel-server

# unpack Buildroot rootfs (ext4 filesystem image) into NFS root
sudo mkdir -p /mnt/rpi-rootfs
sudo mount -o loop output/images/rootfs.ext2 /mnt/rpi-rootfs
sudo cp -a /mnt/rpi-rootfs/. /srv/nfs/rpi4-root
sudo umount /mnt/rpi-rootfs
# make rootfs discoverable by RPI4
echo '/srv/nfs/rpi4-root *(rw,sync,no_subtree_check,no_root_squash)' | sudo tee -a /etc/exports

# enable the service
sudo systemctl enable --now nfs-kernel-server
sudo exportfs -ra
# verify the config applied
sudo exportfs

# enable firewall rules
sudo ufw allow from any to any port nfs
sudo ufw allow from any to any port 111   # rpcbind
sudo ufw allow from any to any port 2049  # nfs
```

Make sure to download Bootlin lab data from https://bootlin.com/training/debugging/. 
Put the unarchived lab data in `/srv/nfs/rpi4-root/root`

```
sudo cp -a debugging-beagleplay-labs/nfsroot/root/* /srv/nfs/rpi4-root/root
```

## Setup TFTP

### Fedora

```bash
# install the TFTP server
sudo dnf install tftp-server

# copy Linux Image and Device Tree to TFTP root
sudo mkdir -p /var/lib/tftpboot
sudo cp output/images/Image /var/lib/tftpboot/
sudo cp output/images/bcm2711-rpi-4-b-merged.dtb /var/lib/tftpboot/   # base dtb + overlay, merged on host

# enable the service and firewall rules
sudo systemctl enable --now tftp.socket
sudo firewall-cmd --add-service=tftp
```

### Ubuntu

```bash
# install the TFTP server
sudo apt install tftpd-hpa

# copy Linux Image and Device Tree to TFTP root
sudo mkdir -p /srv/tftp
sudo cp output/images/Image /srv/tftp/
sudo cp output/images/bcm2711-rpi-4-b-merged.dtb /srv/tftp/

# enable the service and firewall rules
# /etc/default/tftpd-hpa should point TFTP_DIRECTORY at /srv/tftp
sudo systemctl enable --now tftpd-hpa
sudo ufw allow 69/udp
```

## U-Boot

Find your host ip address

```bash
sudo ip -br a
# example:
# lo               UNKNOWN        127.0.0.1/8 ::1/128 
# bridge0          UP             192.168.88.6/24
```

Open `board/raspberrypi4-64/uboot.fragment` and change IP addresses accordingly. 
Change ipaddr to anything within your /24 network.


## Setting Up RPI4 Target

Now you have a netboot-capable host and target.

Next connect UART-to-USB to RPI4. On your host `/dev/ttyUSB0` should appear. 
Run `picocom -b 115200 /dev/ttyUSB0` to connect to the RPI4.

Then connect Ethernet cable to RPI4, and make sure it's directly connected to 
the same router as your host. Use a network switch if needed.

Power RPI4 via USB-C cable. Hit Enter to prevent normal fallback boot into SD card rootfs.

Type the following commands to enable netboot + NFS. Make sure to replace HOST_IP and PI_IP.

```sh
setenv bootcmd 'setenv serverip <HOST_IP>; setenv ipaddr <PI_IP>; tftpboot ${kernel_addr_r} Image; tftpboot ${fdt_addr_r} bcm2711-rpi-4-b-merged.dtb; setenv bootargs console=ttyAMA0,115200 root=/dev/nfs rw nfsroot=<HOST_IP>:/srv/nfs/rpi4-root/,nfsvers=3,tcp ip=dhcp::eth0:off; booti ${kernel_addr_r} - ${fdt_addr_r}'
saveenv
boot
```

If everything went smoothly, you will be greeted by the login prompt.

```
Saving 256 bits of creditable seed for next boot
Starting syslogd: OK
Starting klogd: OK
Running sysctl: OK
Starting network: ip: RTNETLINK answers: File exists
Skipping eth0, used for NFS from 192.168.88.6
FAIL

Welcome to Buildroot
buildroot login: 
```

Note that `FAIL` in the log is benign.

Enter `root` to login. Type `cat /proc/cmdline` to confirm you are using NFS.

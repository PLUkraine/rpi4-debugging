setenv bootcmd 'setenv serverip 192.168.88.3; setenv ipaddr 192.168.88.50; tftpboot ${kernel_addr_r} Image; tftpboot ${fdt_addr_r} bcm2711-rpi-4-b.dtb; setenv bootargs console=ttyAMA0,115200 root=/dev/nfs rw nfsroot=192.168.88.3:/srv/nfs/rpi4-root/,nfsvers=3,tcp ip=dhcp::eth0:off; booti ${kernel_addr_r} - ${fdt_addr_r}'

setenv bootcmd 'setenv serverip 192.168.88.5; setenv ipaddr 192.168.88.50; tftpboot ${kernel_addr_r} Image; tftpboot ${fdt_addr_r} bcm2711-rpi-4-b.dtb; setenv bootargs console=ttyAMA0,115200 root=/dev/nfs rw nfsroot=192.168.88.5:/srv/nfs/rpi4-root/,nfsvers=3,tcp ip=dhcp::eth0:off crashkernel=256M oops=panic nr_cpus=1; booti ${kernel_addr_r} - ${fdt_addr_r}'

setenv bootcmd 'setenv serverip 192.168.88.3; setenv ipaddr 192.168.88.50; tftpboot ${kernel_addr_r} ImageK; tftpboot ${fdt_addr_r} bcm2711-rpi-4-b-custom.dtb; setenv bootargs console=ttyAMA0,115200 root=/dev/nfs rw nfsroot=192.168.88.3:/srv/nfs/rpi4-root/,nfsvers=3,tcp ip=dhcp::eth0:off; booti ${kernel_addr_r} - ${fdt_addr_r}'

kexec -p /root/kexec/Image --command-line="earlycon=pl011,mmio32,0xfe201000 console=ttyAMA0,115200 nr_cpus=1 reset_devices"
echo c > /proc/sysrq-trigger